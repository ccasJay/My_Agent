#include <catch2/catch_test_macros.hpp>
#include "agent/shell.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

using swe_agent::agent::extract_run_command;
using swe_agent::agent::format_process_result;
using swe_agent::agent::run_shell;
using swe_agent::agent::TerminationKind;

TEST_CASE("extract_run_command parses RUN: directives", "[shell]") {
    SECTION("simple RUN: pwd") {
        const auto cmd = extract_run_command("RUN: pwd");
        REQUIRE(cmd.has_value());
        REQUIRE(*cmd == "pwd");
    }

    SECTION("leading spaces before RUN:") {
        const auto cmd = extract_run_command("   \tRUN: pwd");
        REQUIRE(cmd.has_value());
        REQUIRE(*cmd == "pwd");
    }

    SECTION("spaces after RUN: are stripped") {
        // Must yield "ls", not " ls"
        const auto cmd = extract_run_command("RUN:   ls");
        REQUIRE(cmd.has_value());
        REQUIRE(*cmd == "ls");
    }

    SECTION("first of multiple RUN: lines wins") {
        const std::string text =
            "I'll inspect the tree.\n"
            "RUN: pwd\n"
            "Then list files.\n"
            "RUN: ls -la\n";
        const auto cmd = extract_run_command(text);
        REQUIRE(cmd.has_value());
        REQUIRE(*cmd == "pwd");
    }

    SECTION("no RUN: returns nullopt") {
        const auto cmd = extract_run_command(
            "Here is some assistant text with no shell directive.\n"
            "I am done.");
        REQUIRE_FALSE(cmd.has_value());
    }

    SECTION("empty RUN: is skipped; later valid RUN: is used") {
        const std::string text =
            "Trying something.\n"
            "RUN:\n"
            "RUN:   \n"
            "RUN: echo ok\n";
        const auto cmd = extract_run_command(text);
        REQUIRE(cmd.has_value());
        REQUIRE(*cmd == "echo ok");
    }

    SECTION("text before and after the RUN: line") {
        const std::string text =
            "Plan: check working directory first.\n"
            "Then I'll report the path.\n"
            "  RUN: pwd\n"
            "After that we can continue.\n";
        const auto cmd = extract_run_command(text);
        REQUIRE(cmd.has_value());
        REQUIRE(*cmd == "pwd");
    }
}

TEST_CASE("run_shell executes commands and formats output", "[shell]") {
    SECTION("empty command") {
        const auto result = run_shell("");
        REQUIRE(result.termination == TerminationKind::ExecutionError);
        REQUIRE_FALSE(result.success());
        REQUIRE(result.error_message == "[shell] empty command");
    }

    SECTION("echo hello includes prompt and body") {
        const auto result = run_shell("echo hello");
        const std::string out = format_process_result("echo hello", result);
        REQUIRE(result.success());
        REQUIRE(out.find("$ echo hello") != std::string::npos);
        REQUIRE(out.find("hello") != std::string::npos);
        // Successful commands should not append [exit=...]
        REQUIRE(out.find("[exit=") == std::string::npos);
    }

    SECTION("non-zero exit produces [exit=...]") {
        // Portable: `false` is a shell builtin /usr/bin/false on macOS/Linux
        const auto result = run_shell("false");
        const std::string out = format_process_result("false", result);
        REQUIRE(result.termination == TerminationKind::Exited);
        REQUIRE(result.exit_code.has_value());
        REQUIRE(*result.exit_code != 0);
        REQUIRE_FALSE(result.success());
        REQUIRE(out.find("$ false") != std::string::npos);
        REQUIRE(out.find("[exit=") != std::string::npos);
    }

    SECTION("exit 1 also produces [exit=...]") {
        const auto result = run_shell("exit 1");
        const std::string out = format_process_result("exit 1", result);
        REQUIRE(result.termination == TerminationKind::Exited);
        REQUIRE(result.exit_code == 1);
        REQUIRE_FALSE(result.success());
        REQUIRE(out.find("$ exit 1") != std::string::npos);
        REQUIRE(out.find("[exit=") != std::string::npos);
    }

    SECTION("large output is truncated without blocking the child") {
        const auto result = run_shell(
            "awk 'BEGIN { for (i = 0; i < 20000; ++i) print \"xxxxxxxxxx\" }'");

        REQUIRE(result.success());
        REQUIRE(result.truncated);
        REQUIRE(result.output.size() == 16 * 1024);
    }

    SECTION("explicit working directory is isolated to the child") {
        const auto original = std::filesystem::current_path();
        const auto directory = std::filesystem::temp_directory_path() /
            ("swe-agent-shell-" + std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()));
        std::filesystem::create_directories(directory);

        const auto result = run_shell("pwd", directory);

        std::error_code cleanup_error;
        std::filesystem::remove_all(directory, cleanup_error);
        REQUIRE(result.success());
        REQUIRE(result.output.find(directory.string()) != std::string::npos);
        REQUIRE(std::filesystem::current_path() == original);
    }
}
