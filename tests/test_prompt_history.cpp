#include <catch2/catch_test_macros.hpp>

#include "tui/prompt_history.hpp"

#include <string>

TEST_CASE("prompt history navigates entries and restores the draft", "[tui][history]") {
    swe_agent::tui::PromptHistory history;
    history.record("first");
    history.record("second");
    history.record("second");
    REQUIRE(history.size() == 2);

    std::string input = "draft";
    REQUIRE(history.previous(input));
    REQUIRE(input == "second");
    REQUIRE(history.previous(input));
    REQUIRE(input == "first");
    REQUIRE(history.previous(input));
    REQUIRE(input == "first");

    REQUIRE(history.next(input));
    REQUIRE(input == "second");
    REQUIRE(history.next(input));
    REQUIRE(input == "draft");
    REQUIRE_FALSE(history.next(input));
}

TEST_CASE("prompt history can abandon active navigation", "[tui][history]") {
    swe_agent::tui::PromptHistory history;
    history.record("saved");

    std::string input = "draft";
    REQUIRE(history.previous(input));
    history.cancel_navigation();
    input += " edited";

    REQUIRE_FALSE(history.next(input));
    REQUIRE(input == "saved edited");
}
