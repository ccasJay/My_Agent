#include <catch2/catch_test_macros.hpp>

#include "app_cli/cli.hpp"

#include <string>
#include <utility>
#include <vector>

namespace {

swe_agent::cli::Cli::RunOption parse(std::vector<std::string> args) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }

    swe_agent::cli::Cli cli;
    return cli.parse(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

TEST_CASE("CLI defaults to TUI when task is absent", "[cli]") {
    const auto options = parse({"agent"});

    REQUIRE(options.task.empty());
    REQUIRE(options.use_tui());
}

TEST_CASE("CLI selects console mode when task is provided", "[cli]") {
    const auto options = parse({"agent", "-t", "inspect repository"});

    REQUIRE(options.task == "inspect repository");
    REQUIRE_FALSE(options.use_tui());
}

TEST_CASE("CLI selects console mode when an empty task is explicitly provided", "[cli]") {
    const auto options = parse({"agent", "-t", ""});

    REQUIRE(options.task.empty());
    REQUIRE(options.task_provided);
    REQUIRE_FALSE(options.use_tui());
}

TEST_CASE("CLI model override works in TUI and console modes", "[cli]") {
    const auto tui_options = parse({"agent", "-m", "model-a"});
    REQUIRE(tui_options.use_tui());
    REQUIRE(tui_options.model == "model-a");

    const auto console_options =
        parse({"agent", "-t", "task", "-m", "model-b"});
    REQUIRE_FALSE(console_options.use_tui());
    REQUIRE(console_options.model == "model-b");
}
