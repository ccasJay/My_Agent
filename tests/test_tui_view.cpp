#include <catch2/catch_test_macros.hpp>

#include "tui/tui_view.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kCjkEmojiContent =
    "\xE4\xBF\xAE\xE5\xA4\x8D\xE7\x95\x8C\xE9\x9D\xA2"
    "\xF0\x9F\x99\x82\xE5\xB9\xB6\xE4\xBF\x9D\xE7\x95\x99"
    "\xE7\xBB\x88\xE6\x80\x81\xF0\x9F\x9A\x80";
constexpr std::string_view kCjkEmojiModelPrefix =
    "\xE8\xB6\x85\xE9\x95\xBF\xE6\xA8\xA1\xE5\x9E\x8B"
    "\xF0\x9F\x99\x82";

std::string render_to_text(ftxui::Element element, int width, int height) {
    ftxui::Screen screen(width, height);
    ftxui::Render(screen, std::move(element));
    return screen.ToString();
}

ftxui::Screen render_to_screen(
    ftxui::Element element,
    int width,
    int height) {
    ftxui::Screen screen(width, height);
    ftxui::Render(screen, std::move(element));
    return screen;
}

void require_render_within(
    const ftxui::Screen& screen,
    int width,
    int height) {
    REQUIRE(screen.dimx() == width);
    REQUIRE(screen.dimy() == height);
    for (int y = 0; y < screen.dimy(); ++y) {
        int rendered_width = 0;
        for (int x = 0; x < screen.dimx(); ++x) {
            rendered_width += std::max(
                ftxui::string_width(screen.CellAt(x, y).character),
                0);
        }
        REQUIRE(rendered_width <= width);
    }
}

ftxui::Screen render_actual_layout(
    swe_agent::tui::TuiSnapshot snapshot,
    int width,
    int height) {
    std::string task_input;
    auto input = ftxui::Input(&task_input, "Describe a task");
    const std::vector<swe_agent::tui::LogLine> lines{
        {
            .text = "Task",
            .kind = swe_agent::tui::TuiLogKind::Task,
            .block_index = 0,
            .role = swe_agent::tui::LogLineRole::Heading,
        },
        {
            .text = "Keep the real five-region layout visible",
            .kind = swe_agent::tui::TuiLogKind::Assistant,
            .block_index = 0,
            .role = swe_agent::tui::LogLineRole::Content,
        },
    };
    swe_agent::tui::LogViewport viewport;
    (void)viewport.sync(lines.size());
    const bool approval = snapshot.awaiting_approval;

    return render_to_screen(
        swe_agent::tui::render_tui_layout(
            swe_agent::tui::render_header(snapshot, width),
            swe_agent::tui::render_log_panel(
                lines,
                {.begin = 0, .end = lines.size()},
                swe_agent::tui::ActivePane::Prompt,
                0,
                width),
            approval
                ? swe_agent::tui::render_approval_panel(snapshot, width)
                : swe_agent::tui::render_prompt_panel(
                      swe_agent::tui::ActivePane::Prompt,
                      input,
                      task_input,
                      "Describe a task"),
            swe_agent::tui::render_status_bar(
                snapshot,
                swe_agent::tui::ActivePane::Prompt,
                viewport,
                lines.size(),
                width),
            swe_agent::tui::render_shortcuts(
                snapshot.running,
                snapshot.awaiting_approval,
                snapshot.command_mode,
                swe_agent::tui::ActivePane::Prompt,
                viewport.following_tail(),
                width)),
        width,
        height);
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

TEST_CASE("CJK and emoji wrap without exceeding or losing content", "[tui][view]") {
    const std::string content{kCjkEmojiContent};
    const std::vector<swe_agent::tui::TuiLogBlock> blocks{{
        .kind = TuiLogKind::Assistant,
        .heading = "Analysis",
        .summary = content,
    }};

    const auto rendered = make_log_lines(blocks, 0, 8);
    std::string reconstructed;
    for (const auto& line : rendered.lines) {
        if (line.role != LogLineRole::Content) {
            continue;
        }
        REQUIRE(ftxui::string_width(line.text) <= 8);
        reconstructed += line.text;
    }

    REQUIRE(reconstructed == content);
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

TEST_CASE("full header reserves mode and longest status from a long UTF-8 model", "[tui][view]") {
    auto snapshot = snapshot_for_view();
    snapshot.model_name = std::string{kCjkEmojiModelPrefix} +
        "-with-a-name-that-must-not-displace-critical-fields";
    snapshot.status = swe_agent::tui::TuiStatus::StepLimitReached;
    snapshot.status_text = "Step limit reached";

    const std::string rendered = render_to_text(
        swe_agent::tui::render_header(snapshot, 80), 80, 1);

    REQUIRE(rendered.find(kCjkEmojiModelPrefix) != std::string::npos);
    REQUIRE(rendered.find("…") != std::string::npos);
    REQUIRE(rendered.find("Review") != std::string::npos);
    REQUIRE(rendered.find("Step limit reached") != std::string::npos);
}

TEST_CASE("minimal header preserves complete mode before brand and status", "[tui][view]") {
    auto snapshot = snapshot_for_view();
    snapshot.status_text = "Step limit reached";
    snapshot.status = swe_agent::tui::TuiStatus::StepLimitReached;

    const std::string rendered = render_to_text(
        swe_agent::tui::render_header(snapshot, 16), 16, 1);

    REQUIRE(rendered.find("Review") != std::string::npos);
}

TEST_CASE("minimal header keeps longest status at 55 columns", "[tui][view]") {
    auto snapshot = snapshot_for_view();
    snapshot.status_text = "Step limit reached";
    snapshot.status = swe_agent::tui::TuiStatus::StepLimitReached;

    const std::string rendered = render_to_text(
        swe_agent::tui::render_header(snapshot, 55), 55, 1);

    REQUIRE(rendered.find("Review") != std::string::npos);
    REQUIRE(rendered.find("Step limit reached") != std::string::npos);
}

TEST_CASE("full status fields use visible separators", "[tui][view]") {
    swe_agent::tui::LogViewport viewport;
    (void)viewport.sync(12);

    const std::string rendered = render_to_text(
        swe_agent::tui::render_status_bar(
            snapshot_for_view(),
            swe_agent::tui::ActivePane::Prompt,
            viewport,
            12,
            100),
        100,
        1);

    REQUIRE(rendered.find(
                "Model gpt-test │ Step 7 │ Prompt │ Following latest") !=
            std::string::npos);
}

TEST_CASE("full status reserves the reading position from a long model", "[tui][view]") {
    auto snapshot = snapshot_for_view();
    snapshot.model_name =
        "a-model-name-that-is-intentionally-long-enough-to-fill-the-status";
    swe_agent::tui::LogViewport viewport;
    (void)viewport.sync(120);

    const std::string rendered = render_to_text(
        swe_agent::tui::render_status_bar(
            snapshot,
            swe_agent::tui::ActivePane::Scrollback,
            viewport,
            120,
            80),
        80,
        1);

    REQUIRE(rendered.find("Scrollback") != std::string::npos);
    REQUIRE(rendered.find("Following latest") != std::string::npos);
    REQUIRE(rendered.find("120/120") != std::string::npos);
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

TEST_CASE("root layout applies paper and ink to ordinary cells", "[tui][view]") {
    const ftxui::Screen screen = render_to_screen(
        swe_agent::tui::render_tui_layout(
            ftxui::text("header"),
            ftxui::text("ordinary"),
            ftxui::text("action"),
            ftxui::text("status"),
            ftxui::text("shortcuts")),
        20,
        5);

    bool found_ordinary = false;
    for (int y = 0; y < screen.dimy(); ++y) {
        for (int x = 0; x < screen.dimx(); ++x) {
            const auto& cell = screen.CellAt(x, y);
            REQUIRE(cell.background_color == ftxui::Color::RGB(248, 250, 252));
            if (cell.character == "o") {
                found_ordinary = true;
                REQUIRE(cell.foreground_color ==
                        ftxui::Color::RGB(31, 41, 55));
            }
        }
    }
    REQUIRE(found_ordinary);
}

TEST_CASE("real five-region layout stays legible at responsive widths", "[tui][view]") {
    SECTION("full width approval") {
        auto snapshot = snapshot_for_view();
        snapshot.awaiting_approval = true;
        snapshot.pending_command = "cmake --build build --parallel 2";
        const auto screen = render_actual_layout(snapshot, 100, 12);
        const std::string rendered = screen.ToString();

        require_render_within(screen, 100, 12);
        REQUIRE(rendered.find("Review") != std::string::npos);
        REQUIRE(rendered.find("Task") != std::string::npos);
        REQUIRE(rendered.find("cmake --build build --parallel 2") !=
                std::string::npos);
        REQUIRE(rendered.find("Y allow") != std::string::npos);
        REQUIRE(rendered.find("N reject") != std::string::npos);
        REQUIRE(rendered.find("2/2") != std::string::npos);
    }

    SECTION("compact width") {
        const auto screen = render_actual_layout(snapshot_for_view(), 70, 9);
        const std::string rendered = screen.ToString();

        require_render_within(screen, 70, 9);
        REQUIRE(rendered.find("Review") != std::string::npos);
        REQUIRE(rendered.find("Task") != std::string::npos);
        REQUIRE(rendered.find("Describe a task") != std::string::npos);
        REQUIRE(rendered.find("Following latest") != std::string::npos);
        REQUIRE(rendered.find("2/2") != std::string::npos);
    }

    SECTION("minimal width and short approval height") {
        auto snapshot = snapshot_for_view();
        snapshot.awaiting_approval = true;
        snapshot.pending_command = "git status --short";
        const auto screen = render_actual_layout(snapshot, 50, 11);
        const std::string rendered = screen.ToString();

        require_render_within(screen, 50, 11);
        REQUIRE(rendered.find("Review") != std::string::npos);
        REQUIRE(rendered.find("Task") != std::string::npos);
        REQUIRE(rendered.find("git status --short") != std::string::npos);
        REQUIRE(rendered.find("Y allow") != std::string::npos);
        REQUIRE(rendered.find("N reject") != std::string::npos);
        REQUIRE(rendered.find("2/2") != std::string::npos);
    }
}
