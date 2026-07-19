#include "tui/tui_view.hpp"

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/string.hpp>

#include <algorithm>
#include <memory>
#include <sstream>
#include <utility>

namespace swe_agent::tui {
namespace {

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
            });
        }
    }
    if (content.empty()) {
        lines.push_back({
            .kind = kind,
            .block_index = block_index,
        });
    }
}

ftxui::Color log_color(TuiLogKind kind) {
    using ftxui::Color;
    switch (kind) {
    case TuiLogKind::Task: return Color::Blue;
    case TuiLogKind::Assistant: return Color::Cyan;
    case TuiLogKind::Command: return Color::Yellow;
    case TuiLogKind::Observation: return Color::White;
    case TuiLogKind::Final: return Color::Green;
    case TuiLogKind::System: return Color::Magenta;
    case TuiLogKind::Error: return Color::Red;
    }
    return Color::White;
}

ftxui::Color status_color(TuiStatus status) {
    using ftxui::Color;
    switch (status) {
    case TuiStatus::Ready: return Color::Green;
    case TuiStatus::Running: return Color::Cyan;
    case TuiStatus::Stopping: return Color::Yellow;
    case TuiStatus::Stopped:
    case TuiStatus::StepLimitReached:
    case TuiStatus::EmptyResponse: return Color::Yellow;
    case TuiStatus::Error: return Color::Red;
    }
    return Color::White;
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
            .heading = true,
        });

        if (!block.summary.empty()) {
            append_content_lines(
                rendered.lines, block.summary, block.kind, i, max_width);
        }
        if ((!block.foldable || block.expanded) && !block.detail.empty()) {
            append_content_lines(
                rendered.lines, block.detail, block.kind, i, max_width);
        } else if (block.summary.empty()) {
            append_content_lines(
                rendered.lines, block.detail, block.kind, i, max_width);
        }
        rendered.lines.push_back({
            .kind = block.kind,
            .block_index = i,
        });
    }
    return rendered;
}

ftxui::Element render_log_panel(
    const std::vector<LogLine>& lines,
    LogWindow window,
    ActivePane active_pane,
    std::size_t selected_block) {
    using namespace ftxui;
    Elements elements;
    if (lines.empty()) {
        elements.push_back(
            text("Enter a task below to start the agent.") | dim | center);
    } else {
        elements.reserve(window.end - window.begin);
        for (std::size_t i = window.begin; i < window.end; ++i) {
            const LogLine& log_line = lines[i];
            Element line = log_line.heading
                ? text("● " + log_line.text)
                : text(log_line.text);
            if (log_line.heading) {
                line = line | bold | color(log_color(log_line.kind));
            } else if (log_line.kind == TuiLogKind::Final) {
                line = line | bold;
            } else if (log_line.kind == TuiLogKind::System) {
                line = line | dim;
            }
            if (active_pane == ActivePane::Scrollback && log_line.heading &&
                log_line.block_index == selected_block) {
                line = line | inverted;
            }
            elements.push_back(std::move(line));
        }
    }

    Element panel = vbox(std::move(elements)) | flex;
    return active_pane == ActivePane::Scrollback
        ? panel | borderStyled(Color::Cyan)
        : panel | border;
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
        ? Color::Red
        : command_activity ? Color::Green : Color::Cyan;
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
                text(activity.substr(4)) | color(Color::Yellow) | bold);
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
    return hbox(std::move(elements)) | border;
}

ftxui::Element render_prompt_panel(
    ActivePane active_pane,
    const ftxui::Component& input,
    std::string_view task_input,
    std::string_view placeholder) {
    using namespace ftxui;
    Element prompt = hbox({
        text(" ❯ ") | bold | color(Color::Cyan),
        active_pane == ActivePane::Prompt
            ? block_cursor(input->Render()) | flex
            : text(task_input.empty() ? std::string{placeholder}
                                      : std::string{task_input}) |
                flex | dim,
    });
    return active_pane == ActivePane::Prompt
        ? prompt | borderStyled(Color::Cyan)
        : prompt | border;
}

ftxui::Element render_header(const TuiSnapshot& snapshot) {
    using namespace ftxui;
    const Element status = snapshot.running
        ? text("")
        : text("● " + snapshot.status_text + " ") |
            bold | color(status_color(snapshot.status));
    return hbox({
        text(" SWE Agent") | bold | color(Color::Cyan),
        filler(),
        status,
    });
}

ftxui::Element render_status_bar(
    const TuiSnapshot& snapshot,
    ActivePane active_pane,
    const LogViewport& viewport,
    std::size_t line_count) {
    using namespace ftxui;
    return hbox({
        text(" model ") | dim,
        text(snapshot.model_name) | bold,
        text("  │  ") | dim,
        text("step ") | dim,
        text(std::to_string(snapshot.step)) | bold,
        filler(),
        text(active_pane == ActivePane::Prompt
                 ? "prompt  │  "
                 : "scrollback  │  ") |
            dim,
        text(viewport.following_tail()
                 ? "following latest "
                 : "scroll paused ") |
            dim,
        line_count == 0
            ? text("")
            : text(std::to_string(viewport.current_line() + 1) + "/" +
                   std::to_string(line_count) + " ") |
                dim,
    });
}

ftxui::Element render_shortcuts(
    bool running,
    ActivePane active_pane,
    bool following_tail) {
    using namespace ftxui;
    Elements hints;
    if (running) {
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
            shortcut("Ctrl+D", "exit"),
        };
    } else {
        hints = {
            shortcut("↑/↓", "scroll"),
            shortcut("Ctrl+↑/↓", "block"),
            shortcut("Enter", "fold"),
            shortcut("Tab", "prompt"),
            shortcut("PgUp/PgDn", "scroll"),
        };
    }
    if (!following_tail) {
        hints.push_back(shortcut("End", "latest"));
    }

    Elements row;
    for (std::size_t i = 0; i < hints.size(); ++i) {
        if (i > 0) {
            row.push_back(text("  │  ") | dim);
        }
        row.push_back(std::move(hints[i]));
    }
    return hbox(std::move(row));
}

}  // namespace swe_agent::tui
