#include "agent/command_policy.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string_view>

namespace {

swe_agent::agent::PolicyContext make_context() {
    return {
        .working_dir = "/workspace/project",
        .workspace_root = "/workspace/project",
    };
}

}  // namespace

TEST_CASE("command policy requires review for empty commands", "[command-policy]") {
    using swe_agent::agent::PolicyAction;
    using swe_agent::agent::evaluate_command_policy;

    const auto context = make_context();

    for (const std::string_view command : {std::string_view{}, std::string_view{"   \t"}}) {
        const auto result = evaluate_command_policy(command, context);

        REQUIRE(result.action == PolicyAction::RequireReview);
        REQUIRE(result.rule_id == "empty-command");
        REQUIRE_FALSE(result.reason.empty());
    }
}

TEST_CASE("command policy denies configured programs", "[command-policy]") {
    using swe_agent::agent::PolicyAction;
    using swe_agent::agent::evaluate_command_policy;

    constexpr std::array<std::string_view, 6> commands{
        "reboot",
        "shutdown now",
        "rm -rf build",
        "rmdir temp",
        "/sbin/reboot",
        "/usr/bin/rmdir temp",
    };
    const auto context = make_context();

    for (const std::string_view command : commands) {
        const auto result = evaluate_command_policy(command, context);

        INFO("command: " << command);
        REQUIRE(result.action == PolicyAction::Deny);
        REQUIRE_FALSE(result.rule_id.empty());
        REQUIRE_FALSE(result.reason.empty());
    }
}

TEST_CASE("command policy allows unlisted programs", "[command-policy]") {
    using swe_agent::agent::PolicyAction;
    using swe_agent::agent::evaluate_command_policy;

    const auto result = evaluate_command_policy("echo hello", make_context());

    REQUIRE(result.action == PolicyAction::Allow);
    REQUIRE(result.rule_id == "default-allow");
    REQUIRE(result.reason.empty());
}

TEST_CASE("command policy requires review for complex shell syntax", "[command-policy]") {
    using swe_agent::agent::PolicyAction;
    using swe_agent::agent::evaluate_command_policy;

    constexpr std::array<std::string_view, 8> commands{
        "echo ok; rm -rf cache",
        "echo ok && rm cache",
        "echo ok | rm cache",
        "echo $(date)",
        "sudo rm cache",
        "sh -c 'rm cache'",
        "echo ok > out",
        "echo ok\nrm cache",
    };

    for (const std::string_view command : commands) {
        const auto result = evaluate_command_policy(command, make_context());

        INFO("command: " << command);
        REQUIRE(result.action == PolicyAction::RequireReview);
        REQUIRE_FALSE(result.rule_id.empty());
        REQUIRE_FALSE(result.reason.empty());
    }
}

TEST_CASE("command policy denies an absolute rm path", "[command-policy]") {
    using swe_agent::agent::PolicyAction;
    using swe_agent::agent::evaluate_command_policy;

    const auto result = evaluate_command_policy("/bin/rm cache", make_context());

    REQUIRE(result.action == PolicyAction::Deny);
    REQUIRE_FALSE(result.rule_id.empty());
    REQUIRE_FALSE(result.reason.empty());
}
