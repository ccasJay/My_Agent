#include <catch2/catch_test_macros.hpp>

#include "agent/agent_loop.hpp"
#include "agent/history.hpp"
#include "config/agent_loader.hpp"
#include "model/model.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct FakeProvider {
    std::vector<std::string> responses;
    std::vector<swe_agent::model::MSG> seen_histories;
    std::function<void()> after_query;
    std::size_t call = 0;

    swe_agent::model::ModelResponse query(const swe_agent::model::MSG& messages) {
        seen_histories.push_back(messages);
        if (call >= responses.size()) {
            return {""};
        }
        const std::string response = responses[call++];
        if (after_query) {
            after_query();
        }
        return {response};
    }
};

struct CancellableProvider {
    swe_agent::agent::StopSource& stop_source;

    swe_agent::model::ModelResponse query(const swe_agent::model::MSG&) {
        return {"unused"};
    }

    swe_agent::model::ModelResponse query(
        const swe_agent::model::MSG&,
        swe_agent::agent::StopToken) {
        stop_source.request_stop();
        throw swe_agent::agent::OperationCancelled{};
    }
};

static_assert(swe_agent::model::Provider<FakeProvider>);
static_assert(swe_agent::model::Provider<CancellableProvider>);

swe_agent::config::AgentConfig make_cfg(
    std::size_t step_limit = 10,
    std::string system_prompt = "sys-prompt",
    std::string user_prompt = "user-prompt") {
    swe_agent::config::AgentConfig cfg;
    cfg.system_prompt = std::move(system_prompt);
    cfg.user_prompt = std::move(user_prompt);
    cfg.step_limit = step_limit;
    return cfg;
}

bool history_has_role_content(
    const swe_agent::model::MSG& history,
    swe_agent::model::Role role,
    std::string_view needle) {
    for (const auto& m : history) {
        if (m.role == role && m.content.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool has_command_event(
    const std::vector<swe_agent::agent::AgentEvent>& events,
    swe_agent::agent::AgentEventType type,
    std::string_view command) {
    for (const auto& event : events) {
        if (event.type == type && event.command == command) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("agent_loop maps provider cancellation to stopped", "[agent_loop]") {
    swe_agent::agent::StopSource stop_source;
    CancellableProvider provider{stop_source};
    std::vector<swe_agent::agent::AgentEvent> events;
    swe_agent::agent::AgentRunOptions options{
        .on_event = [&events](const swe_agent::agent::AgentEvent& event) {
            events.push_back(event);
        },
        .stop_token = stop_source.token(),
    };

    const auto result = swe_agent::agent::run(provider, make_cfg(), options);

    REQUIRE(result.status == swe_agent::agent::AgentRunStatus::Stopped);
    REQUIRE(events.size() == 1);
    REQUIRE(events.front().type == swe_agent::agent::AgentEventType::Stopped);
}

TEST_CASE("agent_loop respects step_limit after one shell observation", "[agent_loop]") {
    FakeProvider provider;
    provider.responses = {
        "working\nRUN: echo hi\n",
        // 若 step_limit 失效会再 query；故意给第二段避免误完成
        "Conclusion: done\nRUN: echo COMPLETE_TASK\n",
    };

    const auto cfg = make_cfg(/*step_limit=*/1);
    const auto last = swe_agent::agent::run(provider, cfg);

    // 第一轮 observation 后 step=1，下一轮在 query 前因 limit 退出
    REQUIRE(provider.call == 1);
    REQUIRE(provider.seen_histories.size() == 1);
    REQUIRE(last.status == swe_agent::agent::AgentRunStatus::StepLimitReached);
    REQUIRE(last.response.content.find("RUN: echo hi") != std::string::npos);
}

TEST_CASE("agent_loop stops on empty model content", "[agent_loop]") {
    FakeProvider provider;
    provider.responses = {""};

    const auto last = swe_agent::agent::run(provider, make_cfg());

    REQUIRE(last.status == swe_agent::agent::AgentRunStatus::EmptyResponse);
    REQUIRE(last.response.content.empty());
    REQUIRE(provider.call == 1);
    REQUIRE(provider.seen_histories.size() == 1);
}

TEST_CASE("agent_loop appends to caller-owned history", "[agent_loop]") {
    FakeProvider provider;
    provider.responses = {"Conclusion: done\nRUN: echo COMPLETE_TASK\n"};
    auto history = swe_agent::model::MSG{
        {swe_agent::model::Role::System, "session system prompt"},
        {swe_agent::model::Role::User, "previous task"},
    };

    const auto result = swe_agent::agent::run(provider, make_cfg(), history);

    REQUIRE(result.status == swe_agent::agent::AgentRunStatus::Completed);
    REQUIRE(provider.seen_histories.size() == 1);
    REQUIRE(provider.seen_histories.front().size() == 2);
    REQUIRE(history.size() == 3);
    REQUIRE(history.back().role == swe_agent::model::Role::Assistant);
    REQUIRE(history.back().content.find("COMPLETE_TASK") != std::string::npos);
}

TEST_CASE("agent_loop nudges when RUN is missing then completes", "[agent_loop]") {
    FakeProvider provider;
    provider.responses = {
        "I am thinking in plain text only.",
        "Conclusion: finished after format fix\nRUN: echo COMPLETE_TASK\n",
    };

    const auto last = swe_agent::agent::run(provider, make_cfg());

    REQUIRE(provider.call == 2);
    REQUIRE(provider.seen_histories.size() == 2);

    // 第二次 query 的 history 应含 format hint（User）与上轮 Assistant
    const auto& h1 = provider.seen_histories[1];
    REQUIRE(history_has_role_content(h1, swe_agent::model::Role::Assistant, "plain text"));
    REQUIRE(history_has_role_content(h1, swe_agent::model::Role::User, "no valid RUN"));
    REQUIRE(last.status == swe_agent::agent::AgentRunStatus::Completed);
    REQUIRE(last.response.content.find("COMPLETE_TASK") != std::string::npos);
    REQUIRE(last.response.content.find("finished after format fix") != std::string::npos);
}

TEST_CASE("agent_loop rejects COMPLETE_TASK without conclusion then finishes", "[agent_loop]") {
    FakeProvider provider;
    provider.responses = {
        "RUN: echo COMPLETE_TASK\n",
        "Conclusion: now with real text\nRUN: echo COMPLETE_TASK\n",
    };

    const auto last = swe_agent::agent::run(provider, make_cfg());

    REQUIRE(provider.call == 2);
    REQUIRE(provider.seen_histories.size() == 2);

    const auto& h1 = provider.seen_histories[1];
    REQUIRE(history_has_role_content(
        h1, swe_agent::model::Role::User, "COMPLETE_TASK rejected"));
    REQUIRE(history_has_role_content(
        h1, swe_agent::model::Role::User, "no conclusion"));
    REQUIRE(history_has_role_content(
        h1, swe_agent::model::Role::Assistant, "RUN: echo COMPLETE_TASK"));
    REQUIRE(last.status == swe_agent::agent::AgentRunStatus::Completed);
    REQUIRE(last.response.content.find("now with real text") != std::string::npos);
}

TEST_CASE("agent_loop succeeds on conclusion plus COMPLETE_TASK", "[agent_loop]") {
    FakeProvider provider;
    const std::string complete =
        "Conclusion: all good\nRUN: echo COMPLETE_TASK\n";
    provider.responses = {complete};

    const auto last = swe_agent::agent::run(provider, make_cfg());

    REQUIRE(last.status == swe_agent::agent::AgentRunStatus::Completed);
    REQUIRE(last.response.content == complete);
    // 成功 COMPLETE 直接 break，不再二次 query
    REQUIRE(provider.call == 1);
    REQUIRE(provider.seen_histories.size() == 1);
}

TEST_CASE("agent_loop runs normal shell and records Observation", "[agent_loop]") {
    FakeProvider provider;
    provider.responses = {
        "Checking output\nRUN: echo hello\n",
        "Conclusion: saw hello\nRUN: echo COMPLETE_TASK\n",
    };

    const auto last = swe_agent::agent::run(provider, make_cfg());

    REQUIRE(provider.call == 2);
    REQUIRE(provider.seen_histories.size() == 2);

    const auto& h1 = provider.seen_histories[1];
    REQUIRE(history_has_role_content(h1, swe_agent::model::Role::Assistant, "RUN: echo hello"));
    REQUIRE(history_has_role_content(h1, swe_agent::model::Role::User, "Observation:"));
    REQUIRE(history_has_role_content(h1, swe_agent::model::Role::User, "hello"));
    REQUIRE(last.status == swe_agent::agent::AgentRunStatus::Completed);
    REQUIRE(last.response.content.find("saw hello") != std::string::npos);
}

TEST_CASE("agent_loop accepts COMPLETE_TASK with leading spaces on command", "[agent_loop]") {
    FakeProvider provider;
    // extract_run_command 去 RUN: 后空白；is_task_completed 再 trim 整段
    const std::string complete =
        "Conclusion: spaced complete works\nRUN:    echo COMPLETE_TASK\n";
    provider.responses = {complete};

    const auto last = swe_agent::agent::run(provider, make_cfg());

    REQUIRE(last.status == swe_agent::agent::AgentRunStatus::Completed);
    REQUIRE(last.response.content == complete);
    REQUIRE(provider.call == 1);
}

TEST_CASE("agent_loop seeds history with System and User prompts", "[agent_loop]") {
    FakeProvider provider;
    provider.responses = {
        "Conclusion: bootstrapped\nRUN: echo COMPLETE_TASK\n",
    };

    const auto cfg = make_cfg(
        /*step_limit=*/5,
        /*system_prompt=*/"SYSTEM_SEED_PROMPT",
        /*user_prompt=*/"USER_SEED_PROMPT");

    (void)swe_agent::agent::run(provider, cfg);

    REQUIRE(provider.seen_histories.size() == 1);
    const auto& h0 = provider.seen_histories[0];
    REQUIRE(h0.size() == 2);
    REQUIRE(h0[0].role == swe_agent::model::Role::System);
    REQUIRE(h0[0].content == "SYSTEM_SEED_PROMPT");
    REQUIRE(h0[1].role == swe_agent::model::Role::User);
    REQUIRE(h0[1].content == "USER_SEED_PROMPT");
}

TEST_CASE("agent_loop emits ordered events for command and completion", "[agent_loop][events]") {
    FakeProvider provider;
    provider.responses = {
        "Checking\nRUN: echo hello\n",
        "Conclusion: done\nRUN: echo COMPLETE_TASK\n",
    };

    std::vector<swe_agent::agent::AgentEvent> events;
    swe_agent::agent::AgentRunOptions options;
    options.on_event = [&events](const swe_agent::agent::AgentEvent& event) {
        events.push_back(event);
    };

    (void)swe_agent::agent::run(provider, make_cfg(), options);

    using swe_agent::agent::AgentEventType;
    REQUIRE(events.size() == 6);
    REQUIRE(events[0].type == AgentEventType::Assistant);
    REQUIRE(events[1].type == AgentEventType::CommandStarted);
    REQUIRE(events[2].type == AgentEventType::CommandFinished);
    REQUIRE(events[2].command_succeeded == true);
    REQUIRE(events[3].type == AgentEventType::CommandStarted);
    REQUIRE(events[4].type == AgentEventType::CommandFinished);
    REQUIRE(events[4].command_succeeded == true);
    REQUIRE(events[5].type == AgentEventType::Completed);
    REQUIRE(events[0].step == 0);
    REQUIRE(events[3].step == 1);
    REQUIRE(events[5].content.find("Conclusion: done") != std::string::npos);
}

TEST_CASE("agent_loop emits terminal reason events", "[agent_loop][events]") {
    using swe_agent::agent::AgentEvent;
    using swe_agent::agent::AgentEventType;

    SECTION("empty response") {
        FakeProvider provider;
        provider.responses = {""};
        std::vector<AgentEvent> events;
        swe_agent::agent::AgentRunOptions options;
        options.on_event = [&events](const AgentEvent& event) {
            events.push_back(event);
        };

        (void)swe_agent::agent::run(provider, make_cfg(), options);

        REQUIRE(events.size() == 1);
        REQUIRE(events.back().type == AgentEventType::EmptyResponse);
    }

    SECTION("step limit") {
        FakeProvider provider;
        provider.responses = {"Working\nRUN: echo once\n"};
        std::vector<AgentEvent> events;
        swe_agent::agent::AgentRunOptions options;
        options.on_event = [&events](const AgentEvent& event) {
            events.push_back(event);
        };

        (void)swe_agent::agent::run(provider, make_cfg(1), options);

        REQUIRE(events.back().type == AgentEventType::StepLimitReached);
        REQUIRE(events.back().step == 1);
    }
}

TEST_CASE("agent_loop cooperatively stops before querying", "[agent_loop][stop]") {
    FakeProvider provider;
    provider.responses = {"Should not be queried"};
    swe_agent::agent::StopSource stop_source;
    stop_source.request_stop();
    std::vector<swe_agent::agent::AgentEvent> events;
    swe_agent::agent::AgentRunOptions options;
    options.stop_token = stop_source.token();
    options.on_event = [&events](const swe_agent::agent::AgentEvent& event) {
        events.push_back(event);
    };

    const auto result = swe_agent::agent::run(provider, make_cfg(), options);

    REQUIRE(provider.call == 0);
    REQUIRE(result.status == swe_agent::agent::AgentRunStatus::Stopped);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].type == swe_agent::agent::AgentEventType::Stopped);
}

TEST_CASE("agent_loop stops after query without executing command", "[agent_loop][stop]") {
    FakeProvider provider;
    provider.responses = {"Working\nRUN: echo must-not-run\n"};
    swe_agent::agent::StopSource stop_source;
    provider.after_query = [&stop_source] { stop_source.request_stop(); };

    std::vector<swe_agent::agent::AgentEvent> events;
    swe_agent::agent::AgentRunOptions options;
    options.stop_token = stop_source.token();
    options.on_event = [&events](const swe_agent::agent::AgentEvent& event) {
        events.push_back(event);
    };

    const auto result = swe_agent::agent::run(provider, make_cfg(), options);

    REQUIRE(provider.call == 1);
    REQUIRE(result.status == swe_agent::agent::AgentRunStatus::Stopped);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].type == swe_agent::agent::AgentEventType::Stopped);
}

TEST_CASE("agent_loop lets stop win after the completion command", "[agent_loop][stop]") {
    using swe_agent::agent::AgentEvent;
    using swe_agent::agent::AgentEventType;

    FakeProvider provider;
    provider.responses = {
        "Conclusion: complete but stopping\nRUN: echo COMPLETE_TASK\n",
    };
    swe_agent::agent::StopSource stop_source;
    std::vector<AgentEvent> events;
    swe_agent::agent::AgentRunOptions options;
    options.stop_token = stop_source.token();
    options.on_event = [&](const AgentEvent& event) {
        events.push_back(event);
        if (event.type == AgentEventType::CommandFinished) {
            stop_source.request_stop();
        }
    };

    const auto result = swe_agent::agent::run(provider, make_cfg(), options);

    REQUIRE(result.status == swe_agent::agent::AgentRunStatus::Stopped);
    REQUIRE(events.size() == 3);
    REQUIRE(events[0].type == AgentEventType::CommandStarted);
    REQUIRE(events[1].type == AgentEventType::CommandFinished);
    REQUIRE(events[2].type == AgentEventType::Stopped);
}

TEST_CASE("agent_loop executes a command approved by the authorizer", "[agent_loop][authorization]") {
    using swe_agent::agent::AgentEvent;
    using swe_agent::agent::AgentEventType;
    using swe_agent::agent::CommandAction;
    using swe_agent::agent::CommandDecision;
    using swe_agent::agent::CommandRequest;

    FakeProvider provider;
    provider.responses = {
        "Checking approval\nRUN: echo authorized\n",
        "Conclusion: approved command completed\nRUN: echo COMPLETE_TASK\n",
    };

    std::size_t authorizer_calls = 0;
    std::size_t requested_step = 99;
    std::string requested_command;
    std::vector<AgentEvent> events;
    swe_agent::agent::AgentRunOptions options;
    options.authorizer = [&](const CommandRequest& request) {
        ++authorizer_calls;
        requested_step = request.step;
        requested_command = request.command;
        return CommandDecision{
            .action = CommandAction::Approve,
        };
    };
    options.on_event = [&events](const AgentEvent& event) {
        events.push_back(event);
    };

    const auto result = swe_agent::agent::run(provider, make_cfg(), options);

    REQUIRE(result.status == swe_agent::agent::AgentRunStatus::Completed);
    REQUIRE(authorizer_calls == 1);
    REQUIRE(requested_step == 0);
    REQUIRE(requested_command == "echo authorized");
    REQUIRE(has_command_event(
        events, AgentEventType::CommandStarted, "echo authorized"));
    REQUIRE(has_command_event(
        events, AgentEventType::CommandFinished, "echo authorized"));
}

TEST_CASE("agent_loop feeds a rejected command back to the model", "[agent_loop][authorization]") {
    using swe_agent::agent::AgentEvent;
    using swe_agent::agent::AgentEventType;
    using swe_agent::agent::CommandAction;
    using swe_agent::agent::CommandDecision;
    using swe_agent::agent::CommandRequest;

    FakeProvider provider;
    provider.responses = {
        "Trying command\nRUN: echo should-not-run\n",
        "Conclusion: respected rejection\nRUN: echo COMPLETE_TASK\n",
    };

    std::size_t authorizer_calls = 0;
    std::vector<AgentEvent> events;
    swe_agent::agent::AgentRunOptions options;
    options.authorizer = [&](const CommandRequest& request) {
        ++authorizer_calls;
        REQUIRE(request.step == 0);
        REQUIRE(request.command == "echo should-not-run");
        return CommandDecision{
            .action = CommandAction::Reject,
            .rule_id = "test-rejection",
            .reason = "Test rejection reason.",
        };
    };
    options.on_event = [&events](const AgentEvent& event) {
        events.push_back(event);
    };

    const auto result = swe_agent::agent::run(provider, make_cfg(), options);

    REQUIRE(result.status == swe_agent::agent::AgentRunStatus::Completed);
    REQUIRE(authorizer_calls == 1);
    REQUIRE(provider.seen_histories.size() == 2);
    REQUIRE(history_has_role_content(
        provider.seen_histories[1],
        swe_agent::model::Role::User,
        "command rejected"));
    REQUIRE(history_has_role_content(
        provider.seen_histories[1],
        swe_agent::model::Role::User,
        "Test rejection reason"));
    const auto rejected_events = std::count_if(
        events.begin(), events.end(), [](const AgentEvent& event) {
            return event.type == AgentEventType::CommandRejected;
        });
    REQUIRE(rejected_events == 1);
    const auto rejection_event = std::find_if(
        events.begin(), events.end(), [](const AgentEvent& event) {
            return event.type == AgentEventType::CommandRejected;
        });
    REQUIRE(rejection_event->command == "echo should-not-run");
    REQUIRE(rejection_event->rule_id == "test-rejection");
    REQUIRE(rejection_event->content == "Test rejection reason.");
    REQUIRE_FALSE(has_command_event(
        events, AgentEventType::CommandStarted, "echo should-not-run"));
    REQUIRE_FALSE(has_command_event(
        events, AgentEventType::CommandFinished, "echo should-not-run"));
}

TEST_CASE("agent_loop stops when command authorization requests stop", "[agent_loop][authorization]") {
    using swe_agent::agent::AgentEvent;
    using swe_agent::agent::AgentEventType;
    using swe_agent::agent::CommandAction;
    using swe_agent::agent::CommandDecision;
    using swe_agent::agent::CommandRequest;

    FakeProvider provider;
    provider.responses = {"Trying command\nRUN: echo must-not-run\n"};

    std::size_t authorizer_calls = 0;
    std::vector<AgentEvent> events;
    swe_agent::agent::AgentRunOptions options;
    options.authorizer = [&](const CommandRequest&) {
        ++authorizer_calls;
        return CommandDecision{
            .action = CommandAction::Stop,
            .reason = "Stopped during approval",
        };
    };
    options.on_event = [&events](const AgentEvent& event) {
        events.push_back(event);
    };

    const auto result = swe_agent::agent::run(provider, make_cfg(), options);

    REQUIRE(result.status == swe_agent::agent::AgentRunStatus::Stopped);
    REQUIRE(provider.call == 1);
    REQUIRE(authorizer_calls == 1);
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].type == AgentEventType::Assistant);
    REQUIRE(events[1].type == AgentEventType::Stopped);
    REQUIRE_FALSE(has_command_event(
        events, AgentEventType::CommandStarted, "echo must-not-run"));
    REQUIRE_FALSE(has_command_event(
        events, AgentEventType::CommandFinished, "echo must-not-run"));
}

TEST_CASE("agent_loop checkpoints appended assistant history", "[agent_loop][history]") {
    FakeProvider provider;
    provider.responses = {
        "Conclusion: done\nRUN: echo COMPLETE_TASK\n",
    };
    swe_agent::model::MSG history{
        {swe_agent::model::Role::System, "system"},
        {swe_agent::model::Role::User, "task"},
    };
    std::vector<swe_agent::agent::HistoryAppend> appends;
    swe_agent::agent::HistoryHooks hooks;
    hooks.commit_append = [&](const swe_agent::agent::HistoryAppend& append) {
        appends.push_back(append);
    };

    const auto result = swe_agent::agent::run(
        provider,
        make_cfg(),
        history,
        {},
        hooks);

    REQUIRE(result.status == swe_agent::agent::AgentRunStatus::Completed);
    REQUIRE(appends.size() == 1);
    REQUIRE(appends[0].sequence == 2);
    REQUIRE(appends[0].kind == swe_agent::agent::HistoryEntryKind::Assistant);
    REQUIRE(appends[0].message.role == swe_agent::model::Role::Assistant);
    REQUIRE(appends[0].message.content.find("COMPLETE_TASK") != std::string::npos);
}

TEST_CASE("agent_loop rolls back history when checkpoint fails", "[agent_loop][history]") {
    FakeProvider provider;
    provider.responses = {
        "Conclusion: done\nRUN: echo COMPLETE_TASK\n",
    };
    swe_agent::model::MSG history{
        {swe_agent::model::Role::System, "system"},
        {swe_agent::model::Role::User, "task"},
    };
    swe_agent::agent::HistoryHooks hooks;
    hooks.commit_append = [](const swe_agent::agent::HistoryAppend&) {
        throw std::runtime_error{"write failed"};
    };

    REQUIRE_THROWS_AS(
        swe_agent::agent::run(provider, make_cfg(), history, {}, hooks),
        std::runtime_error);
    REQUIRE(history.size() == 2);
    REQUIRE(history.back().content == "task");
}
