#include <catch2/catch_test_macros.hpp>

#include "agent/agent_loop.hpp"
#include "config/agent_loader.hpp"
#include "model/model.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// 静默 cout，避免 shell / step 日志污染测试输出
struct CoutSilence {
    std::ostringstream sink;
    std::streambuf* old = nullptr;

    CoutSilence()
        : old(std::cout.rdbuf(sink.rdbuf())) {}

    ~CoutSilence() {
        std::cout.rdbuf(old);
    }

    CoutSilence(const CoutSilence&) = delete;
    CoutSilence& operator=(const CoutSilence&) = delete;
};

struct FakeProvider {
    std::vector<std::string> responses;
    std::vector<swe_agent::model::MSG> seen_histories;
    std::size_t call = 0;

    swe_agent::model::ModelResponse query(const swe_agent::model::MSG& messages) {
        seen_histories.push_back(messages);
        if (call >= responses.size()) {
            return {""};
        }
        return {responses[call++]};
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
    CoutSilence silence;

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
    REQUIRE(last.content.find("RUN: echo hi") != std::string::npos);
}

TEST_CASE("agent_loop stops on empty model content", "[agent_loop]") {
    CoutSilence silence;

    FakeProvider provider;
    provider.responses = {""};

    const auto last = swe_agent::agent::run(provider, make_cfg());

    REQUIRE(last.content.empty());
    REQUIRE(provider.call == 1);
    REQUIRE(provider.seen_histories.size() == 1);
}

TEST_CASE("agent_loop nudges when RUN is missing then completes", "[agent_loop]") {
    CoutSilence silence;

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
    REQUIRE(last.content.find("COMPLETE_TASK") != std::string::npos);
    REQUIRE(last.content.find("finished after format fix") != std::string::npos);
}

TEST_CASE("agent_loop rejects COMPLETE_TASK without conclusion then finishes", "[agent_loop]") {
    CoutSilence silence;

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
    REQUIRE(last.content.find("now with real text") != std::string::npos);
}

TEST_CASE("agent_loop succeeds on conclusion plus COMPLETE_TASK", "[agent_loop]") {
    CoutSilence silence;

    FakeProvider provider;
    const std::string complete =
        "Conclusion: all good\nRUN: echo COMPLETE_TASK\n";
    provider.responses = {complete};

    const auto last = swe_agent::agent::run(provider, make_cfg());

    REQUIRE(last.content == complete);
    // 成功 COMPLETE 直接 break，不再二次 query
    REQUIRE(provider.call == 1);
    REQUIRE(provider.seen_histories.size() == 1);
}

TEST_CASE("agent_loop runs normal shell and records Observation", "[agent_loop]") {
    CoutSilence silence;

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
    REQUIRE(last.content.find("saw hello") != std::string::npos);
}

TEST_CASE("agent_loop accepts COMPLETE_TASK with leading spaces on command", "[agent_loop]") {
    CoutSilence silence;

    FakeProvider provider;
    // extract_run_command 去 RUN: 后空白；is_task_completed 再 trim 整段
    const std::string complete =
        "Conclusion: spaced complete works\nRUN:    echo COMPLETE_TASK\n";
    provider.responses = {complete};

    const auto last = swe_agent::agent::run(provider, make_cfg());

    REQUIRE(last.content == complete);
    REQUIRE(provider.call == 1);
}

TEST_CASE("agent_loop seeds history with System and User prompts", "[agent_loop]") {
    CoutSilence silence;

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
