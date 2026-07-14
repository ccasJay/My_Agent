#include <catch2/catch_test_macros.hpp>
#include "agent/shell.hpp"

#include <optional>
#include <string>

using swe_agent::agent::extract_run_command;
using swe_agent::agent::run_shell;

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
        const std::string out = run_shell("");
        REQUIRE(out == "[shell] empty command");
    }

    SECTION("echo hello includes prompt and body") {
        const std::string out = run_shell("echo hello");
        REQUIRE(out.find("$ echo hello") != std::string::npos);
        REQUIRE(out.find("hello") != std::string::npos);
        // Successful commands should not append [exit=...]
        REQUIRE(out.find("[exit=") == std::string::npos);
    }

    SECTION("non-zero exit produces [exit=...]") {
        // Portable: `false` is a shell builtin /usr/bin/false on macOS/Linux
        const std::string out = run_shell("false");
        REQUIRE(out.find("$ false") != std::string::npos);
        REQUIRE(out.find("[exit=") != std::string::npos);
    }

    SECTION("exit 1 also produces [exit=...]") {
        const std::string out = run_shell("exit 1");
        REQUIRE(out.find("$ exit 1") != std::string::npos);
        REQUIRE(out.find("[exit=") != std::string::npos);
    }
}
