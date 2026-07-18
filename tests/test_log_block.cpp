#include <catch2/catch_test_macros.hpp>

#include "tui/log_block.hpp"

TEST_CASE("log blocks merge command and observation", "[tui][log-block]") {
    swe_agent::tui::TuiLogBlocks blocks;
    using swe_agent::tui::TuiLogEntry;
    using swe_agent::tui::TuiLogKind;

    REQUIRE(blocks.append({TuiLogEntry{
        .kind = TuiLogKind::Command,
        .heading = "Command",
        .content = "$ echo hello",
    }}) == 0);
    REQUIRE(blocks.size() == 1);
    REQUIRE(blocks.blocks()[0].running);
    REQUIRE(blocks.blocks()[0].expanded);

    REQUIRE(blocks.append({TuiLogEntry{
        .kind = TuiLogKind::Observation,
        .heading = "Observation",
        .content = "$ echo hello\nhello\n",
    }}) == 0);
    REQUIRE(blocks.size() == 1);
    REQUIRE_FALSE(blocks.blocks()[0].running);
    REQUIRE_FALSE(blocks.blocks()[0].expanded);
    REQUIRE(blocks.blocks()[0].summary == "$ echo hello");
    REQUIRE(blocks.blocks()[0].detail == "hello\n");
}

TEST_CASE("log blocks toggle only foldable entries", "[tui][log-block]") {
    swe_agent::tui::TuiLogBlocks blocks;
    using swe_agent::tui::TuiLogEntry;
    using swe_agent::tui::TuiLogKind;

    REQUIRE(blocks.append({
        TuiLogEntry{
            .kind = TuiLogKind::Task,
            .heading = "Task",
            .content = "inspect",
        },
        TuiLogEntry{
            .kind = TuiLogKind::Command,
            .heading = "Command",
            .content = "$ pwd",
        },
    }) == 0);

    REQUIRE_FALSE(blocks.toggle(0));
    REQUIRE(blocks.toggle(1));
    REQUIRE_FALSE(blocks.blocks()[1].expanded);
    REQUIRE_FALSE(blocks.toggle(9));
}

TEST_CASE("unmatched observations remain visible blocks", "[tui][log-block]") {
    swe_agent::tui::TuiLogBlocks blocks;

    REQUIRE(blocks.append({swe_agent::tui::TuiLogEntry{
        .kind = swe_agent::tui::TuiLogKind::Observation,
        .heading = "Observation",
        .content = "standalone",
    }}) == 0);
    REQUIRE(blocks.size() == 1);
    REQUIRE(blocks.blocks()[0].detail == "standalone");
}

TEST_CASE("log blocks report the first changed block", "[tui][log-block]") {
    swe_agent::tui::TuiLogBlocks blocks;
    using swe_agent::tui::TuiLogEntry;
    using swe_agent::tui::TuiLogKind;

    REQUIRE(blocks.append({TuiLogEntry{
        .kind = TuiLogKind::Task,
        .heading = "Task",
        .content = "one",
    }}) == 0);
    REQUIRE(blocks.append({
        TuiLogEntry{
            .kind = TuiLogKind::Assistant,
            .heading = "Assistant",
            .content = "working",
        },
        TuiLogEntry{
            .kind = TuiLogKind::Command,
            .heading = "Command",
            .content = "$ pwd",
        },
    }) == 1);
    REQUIRE(blocks.append({TuiLogEntry{
        .kind = TuiLogKind::Observation,
        .heading = "Observation",
        .content = "$ pwd\n/tmp\n",
    }}) == 2);
    REQUIRE_FALSE(blocks.append({}).has_value());
}

TEST_CASE("log blocks close an abandoned command at a task boundary", "[tui][log-block]") {
    swe_agent::tui::TuiLogBlocks blocks;
    using swe_agent::tui::TuiLogEntry;
    using swe_agent::tui::TuiLogKind;

    REQUIRE(blocks.append({TuiLogEntry{
        .kind = TuiLogKind::Command,
        .heading = "Command",
        .content = "$ broken-command",
    }}) == 0);
    REQUIRE(blocks.blocks()[0].running);

    REQUIRE(blocks.append({
        TuiLogEntry{
            .kind = TuiLogKind::Error,
            .heading = "Error",
            .content = "runner failed",
        },
        TuiLogEntry{
            .kind = TuiLogKind::Task,
            .heading = "Task 2",
            .content = "retry",
        },
    }) == 0);
    REQUIRE_FALSE(blocks.blocks()[0].running);
    REQUIRE_FALSE(blocks.blocks()[0].expanded);

    REQUIRE(blocks.append({TuiLogEntry{
        .kind = TuiLogKind::Observation,
        .heading = "Observation",
        .content = "standalone result",
    }}) == 3);
    REQUIRE(blocks.size() == 4);
    REQUIRE(blocks.blocks()[3].kind == TuiLogKind::Observation);
}
