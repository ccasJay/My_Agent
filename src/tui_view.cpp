#include "tui/tui_view.hpp"

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/string.hpp>

#include <algorithm>
#include <memory>
#include <sstream>
#include <utility>

namespace swe_agent::tui {
namespace {

ftxui::Color paper_color() {
    return ftxui::Color::RGB(248, 250, 252);
}

ftxui::Color ink_color() {
    return ftxui::Color::RGB(31, 41, 55);
}

ftxui::Color blueprint_color() {
    return ftxui::Color::RGB(40, 84, 197);
}

ftxui::Color complete_color() {
    return ftxui::Color::RGB(8, 127, 114);
}

ftxui::Color review_color() {
    return ftxui::Color::RGB(150, 85, 0);
}

ftxui::Color failure_color() {
    return ftxui::Color::RGB(180, 35, 58);
}

std::vector<std::string> wrap_log_line(
    std::string_view line,
    int max_width) {
    std::vector<std::string> rows;
    if (line.empty()) {
        rows.emplace_back();
        return rows;
    }

    max_width = std::max(max_width, 1);
    std::string row;
    int row_width = 0;
    for (const std::string& glyph : ftxui::Utf8ToGlyphs(line)) {
        const int glyph_width = std::max(ftxui::string_width(glyph), 0);
        if (!row.empty() && row_width + glyph_width > max_width) {
            rows.push_back(std::move(row));
            row.clear();
            row_width = 0;
        }
        row += glyph;
        row_width += glyph_width;
    }
    if (!row.empty()) {
        rows.push_back(std::move(row));
    }
    return rows;
}

void append_content_lines(
    std::vector<LogLine>& lines,
    std::string_view content,
    TuiLogKind kind,
    std::size_t block_index,
    int max_width) {
    std::istringstream input{std::string{content}};
    std::string line;
    while (std::getline(input, line)) {
        for (std::string& row : wrap_log_line(line, max_width)) {
            lines.push_back({
                .text = std::move(row),
                .kind = kind,
                .block_index = block_index,
                .role = LogLineRole::Content,
            });
        }
    }
    if (content.empty()) {
        lines.push_back({
            .kind = kind,
            .block_index = block_index,
            .role = LogLineRole::Content,
        });
    }
}

ftxui::Color log_color(TuiLogKind kind) {
    switch (kind) {
    case TuiLogKind::Task: return blueprint_color();
    case TuiLogKind::Assistant: return ink_color();
    case TuiLogKind::Command: return review_color();
    case TuiLogKind::Observation: return ink_color();
    case TuiLogKind::Final: return complete_color();
    case TuiLogKind::System: return blueprint_color();
    case TuiLogKind::Error: return failure_color();
    }
    return ink_color();
}

ftxui::Color status_color(TuiStatus status) {
    switch (status) {
    case TuiStatus::Ready: return complete_color();
    case TuiStatus::Running: return blueprint_color();
    case TuiStatus::Stopping: return review_color();
    case TuiStatus::Stopped:
    case TuiStatus::StepLimitReached:
    case TuiStatus::EmptyResponse: return review_color();
    case TuiStatus::Error: return failure_color();
    }
    return ink_color();
}

ftxui::Element shortcut(std::string key, std::string label) {
    using namespace ftxui;
    return hbox({
        text(std::move(key)) | bold,
        text(":" + std::move(label)) | dim,
    });
}

std::string truncate_to_width(std::string_view value, int max_width) {
    if (max_width <= 0) {
        return {};
    }
    if (ftxui::string_width(value) <= max_width) {
        return std::string{value};
    }
    if (max_width == 1) {
        return "…";
    }

    std::string result;
    int width = 0;
    for (const std::string& glyph : ftxui::Utf8ToGlyphs(value)) {
        const int glyph_width = ftxui::string_width(glyph);
        if (width + glyph_width > max_width - 1) {
            break;
        }
        result += glyph;
        width += glyph_width;
    }
    result += "…";
    return result;
}

// Input v7 将光标形状与插入/覆盖模式绑定。这个装饰器仅覆盖
// 终端光标形状，保留 Input 计算出的光标位置和插入语义。
class BlockCursorNode final : public ftxui::Node {
public:
    explicit BlockCursorNode(ftxui::Element child)
        : Node({std::move(child)}) {}

    void ComputeRequirement() override {
        Node::ComputeRequirement();
        requirement_.focused.cursor_shape =
            ftxui::Screen::Cursor::BlockBlinking;
    }

    void SetBox(ftxui::Box box) override {
        box_ = box;
        children_[0]->SetBox(box);
    }
};

ftxui::Element block_cursor(ftxui::Element child) {
    return std::make_shared<BlockCursorNode>(std::move(child));
}

}  // namespace

TuiLayoutDensity tui_layout_density(int terminal_width) noexcept {
    if (terminal_width >= 80) {
        return TuiLayoutDensity::Full;
    }
    if (terminal_width >= 56) {
        return TuiLayoutDensity::Compact;
    }
    return TuiLayoutDensity::Minimal;
}

std::string_view log_signal(TuiLogKind kind) noexcept {
    switch (kind) {
    case TuiLogKind::Task: return "◆";
    case TuiLogKind::Assistant: return "○";
    case TuiLogKind::Command: return "›";
    case TuiLogKind::Observation:
    case TuiLogKind::System: return "·";
    case TuiLogKind::Final: return "✓";
    case TuiLogKind::Error: return "!";
    }
    return "·";
}

RenderedLog make_log_lines(
    const std::vector<TuiLogBlock>& blocks,
    std::size_t first_block,
    int max_width) {
    RenderedLog rendered;
    for (std::size_t i = first_block; i < blocks.size(); ++i) {
        const TuiLogBlock& block = blocks[i];
        rendered.block_starts.push_back(rendered.lines.size());

        std::string heading = block.heading;
        if (block.foldable) {
            heading = std::string{block.expanded ? "▾ " : "▸ "} + heading;
            heading += block.running ? " · running" : " · done";
        }
        rendered.lines.push_back({
            .text = std::move(heading),
            .kind = block.kind,
            .block_index = i,
            .role = LogLineRole::Heading,
        });

        bool has_content = false;
        if (!block.summary.empty()) {
            append_content_lines(
                rendered.lines, block.summary, block.kind, i, max_width);
            has_content = true;
        }
        if ((!block.foldable || block.expanded) && !block.detail.empty()) {
            append_content_lines(
                rendered.lines, block.detail, block.kind, i, max_width);
            has_content = true;
        }
        if (!has_content) {
            append_content_lines(rendered.lines, "", block.kind, i, max_width);
        }
        rendered.lines.push_back({
            .kind = block.kind,
            .block_index = i,
            .role = LogLineRole::Gap,
        });
    }
    return rendered;
}

ftxui::Element render_log_panel(
    const std::vector<LogLine>& lines,
    LogWindow window,
    ActivePane active_pane,
    std::size_t selected_block,
    int terminal_width) {
    using namespace ftxui;
    Elements elements;
    if (lines.empty()) {
        elements.push_back(
            text(truncate_to_width(
                "Enter a task, or use /sessions to view sessions.",
                std::max(terminal_width - 4, 1))) |
            dim | center);
    } else {
        elements.reserve(window.end - window.begin);
        for (std::size_t i = window.begin; i < window.end; ++i) {
            const LogLine& log_line = lines[i];
            const bool is_heading = log_line.role == LogLineRole::Heading;
            Element line;
            if (is_heading) {
                line = text(std::string{log_signal(log_line.kind)} +
                            " " + log_line.text);
            } else if (log_line.role == LogLineRole::Content) {
                line = text("│ " + log_line.text);
            } else {
                line = text("");
            }
            if (is_heading) {
                line = line | bold | color(log_color(log_line.kind));
            } else if (log_line.role == LogLineRole::Content &&
                       log_line.kind == TuiLogKind::Final) {
                line = line | bold;
            } else if (log_line.role == LogLineRole::Content &&
                       log_line.kind == TuiLogKind::System) {
                line = line | dim;
            }
            if (active_pane == ActivePane::Scrollback && is_heading &&
                log_line.block_index == selected_block) {
                line = line | inverted;
            }
            elements.push_back(std::move(line));
        }
    }

    Element panel = vbox(std::move(elements)) | flex | bgcolor(paper_color());
    return active_pane == ActivePane::Scrollback
        ? panel | borderStyled(blueprint_color())
        : panel | borderStyled(ink_color());
}

ftxui::Element render_run_panel(
    const TuiSnapshot& snapshot,
    const RunStatusAnimation& animation,
    RunStatusAnimation::TimePoint now,
    std::size_t animation_frame,
    int terminal_width) {
    using namespace ftxui;
    const bool command_activity = snapshot.activity_text.starts_with("Run ");
    const Color activity_color = snapshot.status == TuiStatus::Stopping
        ? failure_color()
        : command_activity ? complete_color() : blueprint_color();
    const std::string phase_elapsed =
        format_run_duration(animation.phase_elapsed(now));
    const std::string turn_elapsed =
        format_run_duration(animation.turn_elapsed(now));
    const int inner_width = std::max(terminal_width - 2, 0);

    std::string right_text;
    if (inner_width >= 32) {
        right_text = turn_elapsed + "  ";
    }
    if (inner_width >= 10) {
        right_text += "Esc stop ";
    } else if (inner_width >= 5) {
        right_text += "Esc ";
    }

    const std::string phase_text = inner_width >= 20
        ? " " + phase_elapsed
        : std::string{};
    const int fixed_width = 3 + ftxui::string_width(phase_text) +
        ftxui::string_width(right_text);
    const std::string activity = truncate_to_width(
        format_run_activity(animation.activity()),
        std::max(inner_width - fixed_width, 0));

    Elements elements{
        text(" "),
        text(std::string{run_spinner_frame(animation_frame)}) |
            color(activity_color) | bold,
    };
    if (!activity.empty()) {
        if (command_activity && activity.starts_with("Run ")) {
            elements.push_back(text(" Run ") | dim);
            elements.push_back(
                text(activity.substr(std::string{"Run "}.size())) |
                color(review_color()) | bold);
        } else {
            elements.push_back(
                text(" " + activity) | color(activity_color) | bold);
        }
    }
    if (!phase_text.empty()) {
        elements.push_back(text(phase_text) | dim);
    }
    elements.push_back(filler());
    if (!right_text.empty()) {
        elements.push_back(text(right_text) | dim);
    }
    return hbox(std::move(elements)) | borderStyled(blueprint_color()) |
        bgcolor(paper_color());
}

ftxui::Element render_approval_panel(
    const TuiSnapshot& snapshot,
    int terminal_width) {
    using namespace ftxui;
    const int command_width = std::max(terminal_width - 8, 1);
    const std::string command = truncate_to_width(
        snapshot.pending_command,
        command_width);
    return vbox({
        hbox({
            text(" ! ") | bold | color(review_color()),
            text("Run this command?") | bold,
        }),
        hbox({
            text(" $ ") | dim,
            text(command) | color(review_color()),
        }),
        hbox({
            text(" Y allow ") | bold | color(complete_color()),
            text(" N reject ") | bold | color(failure_color()),
            filler(),
            text("Esc stop ") | dim,
        }),
    }) | borderStyled(review_color()) | bgcolor(paper_color());
}

ftxui::Element render_prompt_panel(
    ActivePane active_pane,
    const ftxui::Component& input,
    std::string_view task_input,
    std::string_view placeholder) {
    using namespace ftxui;
    Element prompt = hbox({
        text(" ❯ ") | bold | color(blueprint_color()),
        active_pane == ActivePane::Prompt
            ? block_cursor(input->Render()) | flex
            : text(task_input.empty() ? std::string{placeholder}
                                      : std::string{task_input}) |
                flex | dim,
    });
    return active_pane == ActivePane::Prompt
        ? prompt | borderStyled(blueprint_color())
        : prompt | borderStyled(ink_color());
}

ftxui::Element render_header(
    const TuiSnapshot& snapshot,
    int terminal_width) {
    using namespace ftxui;
    const TuiLayoutDensity density = tui_layout_density(terminal_width);
    const std::string brand = density == TuiLayoutDensity::Minimal
        ? "SWΞ"
        : "SWΞ / AGENT";
    const Element status = text("● " + snapshot.status_text) |
        bold | color(status_color(snapshot.status));
    Elements header{
        text(" " + brand + " ") | bold | color(blueprint_color()),
    };
    if (density == TuiLayoutDensity::Full) {
        header.push_back(text("Model ") | dim);
        header.push_back(text(snapshot.model_name) | bold | color(ink_color()));
        header.push_back(text("  Step " + std::to_string(snapshot.step)) | dim);
    }
    header.push_back(filler());
    const Color mode_color = snapshot.command_mode == CommandMode::Auto
        ? complete_color()
        : review_color();
    if (density == TuiLayoutDensity::Minimal) {
        header.push_back(text(std::string{command_mode_name(snapshot.command_mode)} + " ") |
                         bold | color(mode_color));
    } else {
        header.push_back(text("Mode ") | bold | color(mode_color));
        header.push_back(text(std::string{command_mode_name(snapshot.command_mode)} + "  ") | dim);
    }
    header.push_back(status);
    return hbox(std::move(header)) | bgcolor(paper_color());
}

ftxui::Element render_status_bar(
    const TuiSnapshot& snapshot,
    ActivePane active_pane,
    const LogViewport& viewport,
    std::size_t line_count,
    int terminal_width) {
    using namespace ftxui;
    const TuiLayoutDensity density = tui_layout_density(terminal_width);
    Elements status;
    if (density == TuiLayoutDensity::Full) {
        status.push_back(text("Model " + snapshot.model_name + "  Step " +
                              std::to_string(snapshot.step)) | dim);
    }
    if (density != TuiLayoutDensity::Minimal) {
        status.push_back(text(active_pane == ActivePane::Prompt
                                  ? "Prompt  │  "
                                  : "Scrollback  │  ") | dim);
        status.push_back(text(viewport.following_tail()
                                  ? "Following latest"
                                  : "Scroll paused") | dim);
    }
    status.push_back(filler());
    if (line_count != 0) {
        status.push_back(text(std::to_string(viewport.current_line() + 1) +
                              "/" + std::to_string(line_count)) | dim);
    }
    return hbox(std::move(status)) | bgcolor(paper_color());
}

ftxui::Element render_shortcuts(
    bool running,
    bool awaiting_approval,
    CommandMode command_mode,
    ActivePane active_pane,
    bool following_tail,
    int terminal_width) {
    using namespace ftxui;
    Elements hints;
    if (awaiting_approval) {
        hints = {
            shortcut("Y", "allow"),
            shortcut("N", "reject"),
            shortcut("Esc", "stop"),
            shortcut("↑/↓", "scroll"),
        };
    } else if (running) {
        hints = {
            shortcut("Esc/Ctrl+C", "stop"),
            shortcut("Ctrl+D", "stop & exit"),
            shortcut("↑/↓", "scroll"),
        };
    } else if (active_pane == ActivePane::Prompt) {
        hints = {
            shortcut("Enter", "send"),
            shortcut("↑/↓", "history"),
            shortcut("Tab", "logs"),
            shortcut(
                "S-Tab",
                "mode " + std::string{command_mode_name(command_mode)}),
            shortcut("Ctrl+D", "exit"),
        };
    } else {
        hints = {
            shortcut("↑/↓", "scroll"),
            shortcut("Ctrl+↑/↓", "block"),
            shortcut("Enter", "fold"),
            shortcut("Tab", "prompt"),
            shortcut(
                "S-Tab",
                "mode " + std::string{command_mode_name(command_mode)}),
            shortcut("PgUp/PgDn", "scroll"),
        };
    }
    if (!following_tail) {
        hints.push_back(shortcut("End", "latest"));
    }

    const TuiLayoutDensity density = tui_layout_density(terminal_width);
    const std::size_t hint_limit = density == TuiLayoutDensity::Full
        ? hints.size()
        : density == TuiLayoutDensity::Compact
        ? std::min<std::size_t>(hints.size(), 3)
        : std::min<std::size_t>(hints.size(), 2);
    Elements row;
    for (std::size_t i = 0; i < hint_limit; ++i) {
        if (i > 0) {
            row.push_back(text("  │  ") | dim);
        }
        row.push_back(std::move(hints[i]));
    }
    return hbox(std::move(row)) | bgcolor(paper_color());
}

}  // namespace swe_agent::tui
