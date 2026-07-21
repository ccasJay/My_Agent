#include "agent/command_authorization.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

swe_agent::agent::PolicyContext make_context() {
    return {
        .working_dir = "/workspace/project",
        .workspace_root = "/workspace/project",
    };
}

swe_agent::agent::CommandRequest make_request(std::string command) {
    return {
        .step = 3,
        .command = std::move(command),
    };
}

}  // namespace

TEST_CASE("共享授权器自动批准允许的命令", "[command-authorization]") {
    using swe_agent::agent::CommandAction;

    const auto decision = swe_agent::agent::authorize_command(
        make_request("echo ok"), make_context(), false, {});

    REQUIRE(decision.action == CommandAction::Approve);
    REQUIRE(decision.rule_id == "default-allow");
}

TEST_CASE("共享授权器在全审核模式调用审核器", "[command-authorization]") {
    using swe_agent::agent::CommandAction;

    std::size_t calls = 0;
    const auto decision = swe_agent::agent::authorize_command(
        make_request("echo ok"),
        make_context(),
        true,
        [&calls](const swe_agent::agent::CommandRequest& request) {
            ++calls;
            REQUIRE(request.command == "echo ok");
            return swe_agent::agent::CommandDecision{
                .action = CommandAction::Approve,
            };
        });

    REQUIRE(calls == 1);
    REQUIRE(decision.action == CommandAction::Approve);
    REQUIRE(decision.rule_id == "default-allow");
}

TEST_CASE("共享授权器在没有审核器时拒绝复杂命令", "[command-authorization]") {
    using swe_agent::agent::CommandAction;

    const auto decision = swe_agent::agent::authorize_command(
        make_request("echo ok; date"), make_context(), false, {});

    REQUIRE(decision.action == CommandAction::Reject);
    REQUIRE(decision.rule_id == "complex-shell-syntax");
    REQUIRE_FALSE(decision.reason.empty());
}

TEST_CASE("共享授权器不允许审核器覆盖拒绝策略", "[command-authorization]") {
    using swe_agent::agent::CommandAction;

    std::size_t calls = 0;
    const auto decision = swe_agent::agent::authorize_command(
        make_request("rm cache"),
        make_context(),
        true,
        [&calls](const swe_agent::agent::CommandRequest&) {
            ++calls;
            return swe_agent::agent::CommandDecision{
                .action = CommandAction::Approve,
            };
        });

    REQUIRE(calls == 0);
    REQUIRE(decision.action == CommandAction::Reject);
    REQUIRE(decision.rule_id == "denied-program");
}

TEST_CASE("共享授权器保留审核器的停止决定", "[command-authorization]") {
    using swe_agent::agent::CommandAction;

    const auto decision = swe_agent::agent::authorize_command(
        make_request("echo ok; date"),
        make_context(),
        false,
        [](const swe_agent::agent::CommandRequest&) {
            return swe_agent::agent::CommandDecision{
                .action = CommandAction::Stop,
                .reason = "User requested stop.",
            };
        });

    REQUIRE(decision.action == CommandAction::Stop);
    REQUIRE(decision.rule_id == "complex-shell-syntax");
}
