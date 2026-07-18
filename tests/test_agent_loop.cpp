#include <catch2/catch_test_macros.hpp>

#include "agent/agent_loop.hpp"
#include "config/agent_loader.hpp"
#include "model/model.hpp"

#include <functional>
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

static_assert(swe_agent::model::Provider<FakeProvider>);

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

}  // namespace

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
    REQUIRE(events[3].type == AgentEventType::CommandStarted);
    REQUIRE(events[4].type == AgentEventType::CommandFinished);
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
