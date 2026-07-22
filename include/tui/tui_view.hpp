#pragma once

#include "tui/log_block.hpp"
#include "tui/log_viewport.hpp"
#include "tui/run_status.hpp"
#include "tui/slash_suggest.hpp"
#include "tui/tui_session.hpp"
#include "tui/tui_state.hpp"

#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace swe_agent::tui {

enum class ActivePane {
    Prompt,
    Scrollback,
};

enum class TuiLayoutDensity {
    Full,
    Compact,
    Minimal,
};

[[nodiscard]] TuiLayoutDensity tui_layout_density(
    int terminal_width) noexcept;

[[nodiscard]] std::string_view log_signal(TuiLogKind kind) noexcept;

enum class LogLineRole {
    Heading,
    Content,
    Gap,
};

struct LogLine {
    std::string text;
    TuiLogKind kind;
    std::size_t block_index{0};
    LogLineRole role{LogLineRole::Content};
};

struct RenderedLog {
    std::vector<LogLine> lines;
    std::vector<std::size_t> block_starts;
};

RenderedLog make_log_lines(
    const std::vector<TuiLogBlock>& blocks,
    std::size_t first_block,
    int max_width);

ftxui::Element render_log_panel(
    const std::vector<LogLine>& lines,
    LogWindow window,
    ActivePane active_pane,
    std::size_t selected_block,
    int terminal_width);

ftxui::Element render_run_panel(
    const TuiSnapshot& snapshot,
    const RunStatusAnimation& animation,
    RunStatusAnimation::TimePoint now,
    std::size_t animation_frame,
    int terminal_width);

ftxui::Element render_approval_panel(
    const TuiSnapshot& snapshot,
    int terminal_width);

ftxui::Element render_prompt_panel(
    ActivePane active_pane,
    const ftxui::Component& input,
    std::string_view task_input,
    std::string_view placeholder);

// Slash 建议下拉面板：由 tui.cpp 与 prompt 做 vbox 组合。
// items 为过滤后的候选；selected_index 高亮；max_visible 默认 6。
// 无匹配时返回 emptyElement，不画边框。
[[nodiscard]] ftxui::Element render_slash_suggest_panel(
    const std::vector<SlashSuggestItem>& items,
    std::size_t selected_index,
    int terminal_width,
    std::size_t max_visible = 6);

ftxui::Element render_header(
    const TuiSnapshot& snapshot,
    int terminal_width);

ftxui::Element render_status_bar(
    const TuiSnapshot& snapshot,
    ActivePane active_pane,
    const LogViewport& viewport,
    std::size_t line_count,
    int terminal_width);

ftxui::Element render_shortcuts(
    bool running,
    bool awaiting_approval,
    CommandMode command_mode,
    ActivePane active_pane,
    bool following_tail,
    int terminal_width);

ftxui::Element render_tui_layout(
    ftxui::Element header,
    ftxui::Element log,
    ftxui::Element action,
    ftxui::Element status,
    ftxui::Element shortcuts);

}  // namespace swe_agent::tui
