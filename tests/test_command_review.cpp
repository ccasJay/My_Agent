#include "app_cli/command_review.hpp"

#include <catch2/catch_test_macros.hpp>

#include <sstream>

namespace {

swe_agent::agent::CommandRequest make_request() {
    return {
        .step = 1,
        .command = "echo ok; date",
    };
}

}  // namespace

TEST_CASE("Console 命令审核接受肯定回答", "[command-review]") {
    std::istringstream input{"Y\n"};
    std::ostringstream output;

    const auto decision = swe_agent::app_cli::review_console_command(
        make_request(), input, output, true);

    REQUIRE(decision.action == swe_agent::agent::CommandAction::Approve);
    REQUIRE(output.str().find("是否批准") != std::string::npos);
}

TEST_CASE("Console 命令审核接受否定回答", "[command-review]") {
    std::istringstream input{"no\n"};
    std::ostringstream output;

    const auto decision = swe_agent::app_cli::review_console_command(
        make_request(), input, output, true);

    REQUIRE(decision.action == swe_agent::agent::CommandAction::Reject);
    REQUIRE(decision.rule_id == "user_rejected");
}

TEST_CASE("Console 命令审核会重试无效回答", "[command-review]") {
    std::istringstream input{"maybe\nyes\n"};
    std::ostringstream output;

    const auto decision = swe_agent::app_cli::review_console_command(
        make_request(), input, output, true);

    REQUIRE(decision.action == swe_agent::agent::CommandAction::Approve);
    REQUIRE(output.str().find("请输入") != std::string::npos);
}

TEST_CASE("Console 命令审核在输入结束时拒绝", "[command-review]") {
    std::istringstream input;
    std::ostringstream output;

    const auto decision = swe_agent::app_cli::review_console_command(
        make_request(), input, output, true);

    REQUIRE(decision.action == swe_agent::agent::CommandAction::Reject);
    REQUIRE(decision.rule_id == "console_eof");
    REQUIRE_FALSE(decision.reason.empty());
}

TEST_CASE("Console 非交互审核不读取输入", "[command-review]") {
    std::istringstream input{"yes\n"};
    std::ostringstream output;

    const auto decision = swe_agent::app_cli::review_console_command(
        make_request(), input, output, false);

    REQUIRE(decision.action == swe_agent::agent::CommandAction::Reject);
    REQUIRE(decision.rule_id == "console_non_interactive");
    REQUIRE(input.peek() == 'y');
}
