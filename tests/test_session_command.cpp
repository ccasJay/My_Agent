#include <catch2/catch_test_macros.hpp>

#include "tui/session_command.hpp"

TEST_CASE("session command parser leaves normal tasks alone", "[tui][session_command]") {
    const auto command = swe_agent::tui::parse_session_command("fix tests");

    REQUIRE(command.kind == swe_agent::tui::SessionCommandKind::None);
}

TEST_CASE("session command parser recognizes management commands", "[tui][session_command]") {
    using swe_agent::tui::SessionCommandKind;

    REQUIRE(
        swe_agent::tui::parse_session_command(" /new ").kind ==
        SessionCommandKind::New);
    REQUIRE(
        swe_agent::tui::parse_session_command("/sessions").kind ==
        SessionCommandKind::List);
    const auto resume =
        swe_agent::tui::parse_session_command("/resume abcdef12");
    REQUIRE(resume.kind == SessionCommandKind::Resume);
    REQUIRE(resume.argument == "abcdef12");
}

TEST_CASE("session command parser reports malformed commands", "[tui][session_command]") {
    using swe_agent::tui::SessionCommandKind;

    const auto missing_id = swe_agent::tui::parse_session_command("/resume");
    REQUIRE(missing_id.kind == SessionCommandKind::Invalid);
    REQUIRE_FALSE(missing_id.error.empty());

    const auto unknown = swe_agent::tui::parse_session_command("/unknown");
    REQUIRE(unknown.kind == SessionCommandKind::Invalid);
    REQUIRE_FALSE(unknown.error.empty());

    const auto prefixed =
        swe_agent::tui::parse_session_command("/resumeabcdef12");
    REQUIRE(prefixed.kind == SessionCommandKind::Invalid);
}
