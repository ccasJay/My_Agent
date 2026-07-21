#include <catch2/catch_test_macros.hpp>

#include "tui/tui_view.hpp"

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <string>
#include <vector>

namespace {

std::string render_to_text(ftxui::Element element, int width, int height) {
    ftxui::Screen screen(width, height);
    ftxui::Render(screen, std::move(element));
    return screen.ToString();
}

swe_agent::tui::TuiSnapshot snapshot_for_view() {
    return {
        .status_text = "Ready",
        .model_name = "gpt-test",
        .status = swe_agent::tui::TuiStatus::Ready,
        .step = 7,
        .command_mode = swe_agent::tui::CommandMode::Review,
    };
}

}  // namespace

using swe_agent::tui::TuiLayoutDensity;
using swe_agent::tui::TuiLogKind;
using swe_agent::tui::LogLineRole;
using swe_agent::tui::log_signal;
using swe_agent::tui::make_log_lines;
using swe_agent::tui::tui_layout_density;

TEST_CASE("TUI layout density follows terminal width boundaries", "[tui][view]") {
    REQUIRE(tui_layout_density(80) == TuiLayoutDensity::Full);
    REQUIRE(tui_layout_density(79) == TuiLayoutDensity::Compact);
    REQUIRE(tui_layout_density(56) == TuiLayoutDensity::Compact);
    REQUIRE(tui_layout_density(55) == TuiLayoutDensity::Minimal);
}

TEST_CASE("TUI log kinds use fixed signal glyphs", "[tui][view]") {
    REQUIRE(log_signal(TuiLogKind::Task) == "◆");
    REQUIRE(log_signal(TuiLogKind::Assistant) == "○");
    REQUIRE(log_signal(TuiLogKind::Command) == "›");
    REQUIRE(log_signal(TuiLogKind::Observation) == "·");
    REQUIRE(log_signal(TuiLogKind::Final) == "✓");
    REQUIRE(log_signal(TuiLogKind::System) == "·");
    REQUIRE(log_signal(TuiLogKind::Error) == "!");
}

TEST_CASE("log lines mark a normal task heading content and gap", "[tui][view]") {
    const std::vector<swe_agent::tui::TuiLogBlock> blocks{{
        .kind = TuiLogKind::Task,
        .heading = "Task",
        .summary = "Plan the change",
        .detail = "Then implement it",
    }};

    const auto rendered = make_log_lines(blocks, 0, 80);

    REQUIRE(rendered.block_starts == std::vector<std::size_t>{0});
    REQUIRE(rendered.lines.size() == 4);
    REQUIRE(rendered.lines[0].role == LogLineRole::Heading);
    REQUIRE(rendered.lines[1].role == LogLineRole::Content);
    REQUIRE(rendered.lines[2].role == LogLineRole::Content);
    REQUIRE(rendered.lines[3].role == LogLineRole::Gap);
    REQUIRE(rendered.lines[0].text == "Task");
    REQUIRE(rendered.lines[1].text == "Plan the change");
    REQUIRE(rendered.lines[2].text == "Then implement it");
}

TEST_CASE("log lines keep a content row for an empty body", "[tui][view]") {
    const std::vector<swe_agent::tui::TuiLogBlock> blocks{{
        .kind = TuiLogKind::Task,
        .heading = "Task",
    }};

    const auto rendered = make_log_lines(blocks, 0, 80);

    REQUIRE(rendered.lines.size() == 3);
    REQUIRE(rendered.lines[0].role == LogLineRole::Heading);
    REQUIRE(rendered.lines[1].role == LogLineRole::Content);
    REQUIRE(rendered.lines[1].text.empty());
    REQUIRE(rendered.lines[2].role == LogLineRole::Gap);
}

TEST_CASE("collapsed command keeps summary and hides detail", "[tui][view]") {
    const std::vector<swe_agent::tui::TuiLogBlock> blocks{{
        .kind = TuiLogKind::Command,
        .heading = "Command",
        .summary = "git status",
        .detail = "On branch feature/tui-signal-ledger-redesign",
        .foldable = true,
        .expanded = false,
    }};

    const auto rendered = make_log_lines(blocks, 0, 80);

    REQUIRE(rendered.lines.size() == 3);
    REQUIRE(rendered.lines[0].role == LogLineRole::Heading);
    REQUIRE(rendered.lines[1].role == LogLineRole::Content);
    REQUIRE(rendered.lines[2].role == LogLineRole::Gap);
    REQUIRE(rendered.lines[1].text == "git status");
    REQUIRE(rendered.lines[0].text.find("done") != std::string::npos);
}

TEST_CASE("folded command labels running state", "[tui][view]") {
    const std::vector<swe_agent::tui::TuiLogBlock> blocks{{
        .kind = TuiLogKind::Command,
        .heading = "Command",
        .summary = "git status",
        .foldable = true,
        .expanded = false,
        .running = true,
    }};

    const auto rendered = make_log_lines(blocks, 0, 80);

    REQUIRE(rendered.lines[0].text.find("running") != std::string::npos);
}

TEST_CASE("UTF-8 newlines produce content rows", "[tui][view]") {
    const std::vector<swe_agent::tui::TuiLogBlock> blocks{{
        .kind = TuiLogKind::Assistant,
        .heading = "Analysis",
        .summary = "first\nsecond",
    }};

    const auto rendered = make_log_lines(blocks, 0, 80);

    REQUIRE(rendered.lines.size() == 4);
    REQUIRE(rendered.lines[1].role == LogLineRole::Content);
    REQUIRE(rendered.lines[2].role == LogLineRole::Content);
    REQUIRE(rendered.lines[1].text == "first");
    REQUIRE(rendered.lines[2].text == "second");
    REQUIRE(rendered.lines[3].role == LogLineRole::Gap);
}

TEST_CASE("full header shows signal-ledger metadata", "[tui][view]") {
    const std::string rendered = render_to_text(
        swe_agent::tui::render_header(snapshot_for_view(), 100), 100, 3);

    REQUIRE(rendered.find("SWΞ / AGENT") != std::string::npos);
    REQUIRE(rendered.find("gpt-test") != std::string::npos);
    REQUIRE(rendered.find("Review") != std::string::npos);
}

TEST_CASE("compact header hides the model but keeps review mode", "[tui][view]") {
    const std::string rendered = render_to_text(
        swe_agent::tui::render_header(snapshot_for_view(), 70), 70, 3);

    REQUIRE(rendered.find("SWΞ / AGENT") != std::string::npos);
    REQUIRE(rendered.find("gpt-test") == std::string::npos);
    REQUIRE(rendered.find("Review") != std::string::npos);
}

TEST_CASE("minimal header uses the abbreviated brand", "[tui][view]") {
    const std::string rendered = render_to_text(
        swe_agent::tui::render_header(snapshot_for_view(), 50), 50, 3);

    auto auto_snapshot = snapshot_for_view();
    auto_snapshot.command_mode = swe_agent::tui::CommandMode::Auto;
    const std::string auto_rendered = render_to_text(
        swe_agent::tui::render_header(auto_snapshot, 50), 50, 3);

    REQUIRE(rendered.find("SWΞ") != std::string::npos);
    REQUIRE(rendered.find("SWΞ / AGENT") == std::string::npos);
    REQUIRE(rendered.find("Review") != std::string::npos);
    REQUIRE(auto_rendered.find("Auto") != std::string::npos);
    REQUIRE(auto_rendered.find("Review") == std::string::npos);
}

TEST_CASE("narrow log content reserves space for borders and rail", "[tui][view]") {
    const std::vector<swe_agent::tui::TuiLogBlock> blocks{{
        .kind = TuiLogKind::Assistant,
        .heading = "Assistant",
        .summary = "abcdefghij",
    }};
    constexpr int kTerminalWidth = 12;
    const auto rendered = make_log_lines(
        blocks, 0, kTerminalWidth - 2 - 2);
    const std::string panel = render_to_text(
        swe_agent::tui::render_log_panel(
            rendered.lines,
            {.begin = 0, .end = rendered.lines.size()},
            swe_agent::tui::ActivePane::Prompt,
            0,
            kTerminalWidth),
        kTerminalWidth,
        8);

    REQUIRE(rendered.lines[1].text == "abcdefgh");
    REQUIRE(rendered.lines[2].text == "ij");
    REQUIRE(panel.find("│ abcdefgh") != std::string::npos);
    REQUIRE(panel.find("│ ij") != std::string::npos);
}

TEST_CASE("signal-ledger log panel renders rail and empty guidance", "[tui][view]") {
    const std::vector<swe_agent::tui::LogLine> lines{
        {
            .text = "Task",
            .kind = TuiLogKind::Task,
            .role = LogLineRole::Heading,
        },
        {
            .text = "Final content",
            .kind = TuiLogKind::Final,
            .role = LogLineRole::Content,
        },
        {
            .text = "Final",
            .kind = TuiLogKind::Final,
            .role = LogLineRole::Heading,
        },
    };
    const auto panel = swe_agent::tui::render_log_panel(
        lines, {.begin = 0, .end = lines.size()},
        swe_agent::tui::ActivePane::Prompt, 0, 80);
    const std::string rendered = render_to_text(panel, 80, 6);
    const std::string empty = render_to_text(
        swe_agent::tui::render_log_panel(
            {}, {.begin = 0, .end = 0}, swe_agent::tui::ActivePane::Prompt,
            0, 80),
        80, 4);

    REQUIRE(rendered.find("◆") != std::string::npos);
    REQUIRE(rendered.find("│") != std::string::npos);
    REQUIRE(rendered.find("✓") != std::string::npos);
    REQUIRE(empty.find("Enter a task, or use /sessions to view sessions.") !=
            std::string::npos);
}

TEST_CASE("approval panel presents decision labels and command", "[tui][view]") {
    auto snapshot = snapshot_for_view();
    snapshot.awaiting_approval = true;
    snapshot.pending_command = "git status --short";
    const std::string rendered = render_to_text(
        swe_agent::tui::render_approval_panel(snapshot, 100), 100, 6);

    REQUIRE(rendered.find("Run this command?") != std::string::npos);
    REQUIRE(rendered.find("git status --short") != std::string::npos);
    REQUIRE(rendered.find("Y allow") != std::string::npos);
    REQUIRE(rendered.find("N reject") != std::string::npos);
    REQUIRE(rendered.find("Esc stop") != std::string::npos);
}
