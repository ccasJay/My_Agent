#include <catch2/catch_test_macros.hpp>

#include "tui/slash_registry.hpp"
#include "tui/slash_suggest.hpp"

#include <string>
#include <string_view>
#include <vector>

using swe_agent::tui::SlashRegistry;
using swe_agent::tui::SlashSuggestResult;
using swe_agent::tui::apply_slash_completion;
using swe_agent::tui::completion_cursor_after;
using swe_agent::tui::evaluate_slash_suggest;
using swe_agent::tui::register_builtin_slash_commands;

namespace {

SlashRegistry make_builtin_registry() {
    SlashRegistry registry;
    register_builtin_slash_commands(registry);
    return registry;
}

std::vector<std::string> match_names(const SlashSuggestResult& result) {
    std::vector<std::string> names;
    names.reserve(result.matches.size());
    for (const auto& item : result.matches) {
        names.push_back(item.name);
    }
    return names;
}

}  // namespace

TEST_CASE("slash suggest stays closed without an active slash token",
          "[tui][slash][suggest]") {
    const auto registry = make_builtin_registry();
    const auto commands = registry.list();

    SECTION("empty input") {
        const auto result = evaluate_slash_suggest("", 0, commands);
        REQUIRE_FALSE(result.open);
        REQUIRE(result.matches.empty());
    }

    SECTION("plain text without slash") {
        const std::string input = "hello";
        const auto result =
            evaluate_slash_suggest(input, static_cast<int>(input.size()), commands);
        REQUIRE_FALSE(result.open);
        REQUIRE(result.matches.empty());
    }

    SECTION("cursor after whitespace only") {
        const auto result = evaluate_slash_suggest("   ", 2, commands);
        REQUIRE_FALSE(result.open);
        REQUIRE(result.matches.empty());
    }
}

TEST_CASE("slash suggest lists all builtins for bare slash",
          "[tui][slash][suggest]") {
    const auto registry = make_builtin_registry();
    const auto commands = registry.list();

    const std::string input = "/";
    const auto result =
        evaluate_slash_suggest(input, static_cast<int>(input.size()), commands);

    REQUIRE(result.open);
    REQUIRE(result.prefix.empty());
    REQUIRE(result.token_begin == 0);
    REQUIRE(result.token_end == 1);
    REQUIRE(match_names(result) ==
            std::vector<std::string>{"new", "sessions", "resume", "help"});
}

TEST_CASE("slash suggest filters by case-sensitive name prefix",
          "[tui][slash][suggest]") {
    const auto registry = make_builtin_registry();
    const auto commands = registry.list();

    SECTION("/he matches only help") {
        const std::string input = "/he";
        const auto result =
            evaluate_slash_suggest(input, static_cast<int>(input.size()), commands);
        REQUIRE(result.open);
        REQUIRE(result.prefix == "he");
        REQUIRE(result.token_begin == 0);
        REQUIRE(result.token_end == 3);
        REQUIRE(match_names(result) == std::vector<std::string>{"help"});
    }

    SECTION("/H is case-sensitive and closes") {
        const std::string input = "/H";
        const auto result =
            evaluate_slash_suggest(input, static_cast<int>(input.size()), commands);
        REQUIRE_FALSE(result.open);
        REQUIRE(result.matches.empty());
    }

    SECTION("/se matches sessions") {
        const std::string input = "/se";
        const auto result =
            evaluate_slash_suggest(input, static_cast<int>(input.size()), commands);
        REQUIRE(result.open);
        REQUIRE(match_names(result) == std::vector<std::string>{"sessions"});
    }
}

TEST_CASE("slash suggest uses the slash token under the cursor",
          "[tui][slash][suggest]") {
    const auto registry = make_builtin_registry();
    const auto commands = registry.list();

    SECTION("task /se with cursor in /se") {
        const std::string input = "task /se";
        // Cursor at end of "/se"
        const int cursor = static_cast<int>(input.size());
        const auto result = evaluate_slash_suggest(input, cursor, commands);

        REQUIRE(result.open);
        REQUIRE(result.prefix == "se");
        REQUIRE(result.token_begin == 5);
        REQUIRE(result.token_end == 8);
        REQUIRE(match_names(result) == std::vector<std::string>{"sessions"});
    }

    SECTION("cursor mid-token still filters on full token prefix") {
        const std::string input = "task /se";
        // Cursor between 's' and 'e' in "/se" (byte index of 'e')
        const int cursor = 7;
        const auto result = evaluate_slash_suggest(input, cursor, commands);

        REQUIRE(result.open);
        REQUIRE(result.prefix == "se");
        REQUIRE(result.token_begin == 5);
        REQUIRE(result.token_end == 8);
        REQUIRE(match_names(result) == std::vector<std::string>{"sessions"});
    }
}

TEST_CASE("slash suggest closes after command token and trailing space",
          "[tui][slash][suggest]") {
    const auto registry = make_builtin_registry();
    const auto commands = registry.list();

    const std::string input = "/resume ";
    const int cursor = static_cast<int>(input.size());
    const auto result = evaluate_slash_suggest(input, cursor, commands);

    REQUIRE_FALSE(result.open);
    REQUIRE(result.matches.empty());
}

TEST_CASE("apply_slash_completion rewrites the active token",
          "[tui][slash][suggest]") {
    const auto registry = make_builtin_registry();
    const auto commands = registry.list();

    SECTION("/he + help completes without trailing space") {
        const std::string input = "/he";
        const auto result =
            evaluate_slash_suggest(input, static_cast<int>(input.size()), commands);
        REQUIRE(result.open);
        REQUIRE(result.matches.size() == 1);
        REQUIRE(result.matches[0].name == "help");

        const auto completed = apply_slash_completion(input, result, 0);
        REQUIRE(completed == "/help");
    }

    SECTION("/re + resume completes with trailing space for args") {
        const std::string input = "/re";
        const auto result =
            evaluate_slash_suggest(input, static_cast<int>(input.size()), commands);
        REQUIRE(result.open);
        REQUIRE(result.matches.size() == 1);
        REQUIRE(result.matches[0].name == "resume");

        const auto completed = apply_slash_completion(input, result, 0);
        REQUIRE(completed == "/resume ");
    }

    SECTION("completion mid-line preserves surrounding text") {
        const std::string input = "note /se more";
        // Cursor on "/se" (after 'e')
        const int cursor = 8;
        const auto result = evaluate_slash_suggest(input, cursor, commands);
        REQUIRE(result.open);
        REQUIRE(match_names(result) == std::vector<std::string>{"sessions"});

        const auto completed = apply_slash_completion(input, result, 0);
        REQUIRE(completed == "note /sessions more");
    }
}

TEST_CASE("completion_cursor_after places cursor at end of completed input",
          "[tui][slash][suggest]") {
    const std::string completed = "/resume ";
    REQUIRE(completion_cursor_after(completed) ==
            static_cast<int>(completed.size()));
    REQUIRE(completion_cursor_after("/help") == 5);
    REQUIRE(completion_cursor_after("") == 0);
}

TEST_CASE("slash suggest refilter narrows matches via evaluate",
          "[tui][slash][suggest]") {
    const auto registry = make_builtin_registry();
    const auto commands = registry.list();

    // Conceptual refilter: re-run evaluate as the prefix grows.
    // UI selected index is outside pure helpers; tests only assert match lists.
    const auto open_all =
        evaluate_slash_suggest("/", 1, commands);
    REQUIRE(open_all.open);
    REQUIRE(match_names(open_all) ==
            std::vector<std::string>{"new", "sessions", "resume", "help"});

    const auto after_s =
        evaluate_slash_suggest("/s", 2, commands);
    REQUIRE(after_s.open);
    REQUIRE(match_names(after_s) == std::vector<std::string>{"sessions"});

    // Further typing that leaves no match closes the popup.
    const auto after_sx =
        evaluate_slash_suggest("/sx", 3, commands);
    REQUIRE_FALSE(after_sx.open);
    REQUIRE(after_sx.matches.empty());

    // Prefix that still matches multiple shrinks from full list without reordering.
    // (With current builtins only one name starts with each letter; use bare / then /re)
    const auto after_r =
        evaluate_slash_suggest("/r", 2, commands);
    REQUIRE(after_r.open);
    REQUIRE(match_names(after_r) == std::vector<std::string>{"resume"});

    const auto after_re =
        evaluate_slash_suggest("/re", 3, commands);
    REQUIRE(after_re.open);
    REQUIRE(match_names(after_re) == std::vector<std::string>{"resume"});
}
