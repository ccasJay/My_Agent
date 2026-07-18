#include "tui/tui.hpp"

#include "agent/agent_loop.hpp"
#include "model/model.hpp"
#include "tui/log_block.hpp"
#include "tui/log_viewport.hpp"
#include "tui/prompt_history.hpp"
#include "tui/run_status.hpp"
#include "tui/tui_session.hpp"
#include "tui/tui_state.hpp"

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/string.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace swe_agent::tui {
namespace {

struct LogLine {
    std::string text;
    TuiLogKind kind;
    std::size_t block_index{0};
    bool heading{false};
};

struct RenderedLog {
    std::vector<LogLine> lines;
    std::vector<std::size_t> block_starts;
};

enum class ActivePane {
    Prompt,
    Scrollback,
};

void append_content_lines(
    std::vector<LogLine>& lines,
    std::string_view content,
    TuiLogKind kind,
    std::size_t block_index) {
    std::istringstream input{std::string{content}};
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back({
            .text = std::move(line),
            .kind = kind,
            .block_index = block_index,
        });
    }
    if (content.empty()) {
        lines.push_back({
            .kind = kind,
            .block_index = block_index,
        });
    }
}

RenderedLog make_log_lines(
    const std::vector<TuiLogBlock>& blocks,
    std::size_t first_block) {
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
                rendered.lines,
                block.summary,
                block.kind,
                i);
        }
        if ((!block.foldable || block.expanded) && !block.detail.empty()) {
            append_content_lines(
                rendered.lines,
                block.detail,
                block.kind,
                i);
        } else if (block.summary.empty()) {
            append_content_lines(rendered.lines, block.detail, block.kind, i);
        }
        rendered.lines.push_back({
            .kind = block.kind,
            .block_index = i,
        });
    }
    return rendered;
}

ftxui::Color log_color(TuiLogKind kind) {
    using ftxui::Color;

    switch (kind) {
    case TuiLogKind::Task:
        return Color::Blue;
    case TuiLogKind::Assistant:
        return Color::Cyan;
    case TuiLogKind::Command:
        return Color::Yellow;
    case TuiLogKind::Observation:
        return Color::White;
    case TuiLogKind::Final:
        return Color::Green;
    case TuiLogKind::System:
        return Color::Magenta;
    case TuiLogKind::Error:
        return Color::Red;
    }
    return Color::White;
}

ftxui::Color status_color(TuiStatus status) {
    using ftxui::Color;

    switch (status) {
    case TuiStatus::Ready:
        return Color::Green;
    case TuiStatus::Running:
        return Color::Cyan;
    case TuiStatus::Stopping:
        return Color::Yellow;
    case TuiStatus::Stopped:
    case TuiStatus::StepLimitReached:
    case TuiStatus::EmptyResponse:
        return Color::Yellow;
    case TuiStatus::Error:
        return Color::Red;
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

}  // namespace

int run(
    model::ModelClient& client,
    const config::AgentConfig& agent_cfg,
    const std::string& model_name) {
    using namespace ftxui;

    App app = App::Fullscreen();
    app.ForceHandleCtrlC(false);
    std::string task_input;
    PromptHistory prompt_history;
    RunStatusAnimation run_status_animation;
    bool idle_ctrl_c_armed = false;
    // animator 仅请求刷新；所有 FTXUI/viewport 状态仍留在主线程。
    std::atomic_bool animation_active{true};
    std::atomic_bool scroll_animation_pending{false};
    std::atomic_bool refresh_event_queued{false};
    std::atomic_bool animation_event_queued{false};
    std::atomic_size_t animation_frame{0};
    std::mutex animation_mutex;
    std::condition_variable animation_cv;
    std::thread animator;
    // 日志条目先增量合并成语义块；Element 树仅在内容或焦点变化时重建。
    TuiLogBlocks log_blocks;
    std::vector<LogLine> cached_log_lines;
    std::vector<std::size_t> cached_block_starts;
    std::size_t cached_log_revision = 0;
    std::size_t selected_block = 0;
    ActivePane active_pane = ActivePane::Prompt;
    LogViewport log_viewport;
    Element cached_log_panel;
    bool log_panel_dirty = true;
    int cached_terminal_width = 0;
    int cached_terminal_height = 0;
    bool exit_after_stop = false;
    const Event refresh_event = Event::Special("swe-agent-refresh");
    const Event animation_event = Event::Special("swe-agent-animation");

    auto post_refresh = [&app, &refresh_event, &refresh_event_queued] {
        // PostEvent 是 Worker 唯一接触 UI 的入口，实际绘制仍由主线程完成。
        if (!refresh_event_queued.exchange(true)) {
            try {
                app.PostEvent(refresh_event);
            } catch (...) {
                refresh_event_queued.store(false);
                throw;
            }
        }
    };

    TuiSession session{
        model_name,
        [&client, &agent_cfg](
            const std::string& task,
            const agent::AgentRunOptions& options) {
            config::AgentConfig task_cfg = agent_cfg;
            task_cfg.user_prompt = task;
            return agent::run(client, task_cfg, options);
        },
        post_refresh,
    };

    auto start_task = [&] {
        const std::string task = task_input;
        if (!session.start(task)) {
            return;
        }

        prompt_history.record(task);
        task_input.clear();
        idle_ctrl_c_armed = false;
        log_viewport.follow_tail();
        exit_after_stop = false;
        animation_cv.notify_one();
    };

    auto input = Input(&task_input, "Describe a task and press Enter");

    auto rebuild_log_lines_from = [&](std::size_t first_block) {
        first_block = std::min(first_block, log_blocks.size());
        const std::size_t first_line =
            first_block < cached_block_starts.size()
            ? cached_block_starts[first_block]
            : cached_log_lines.size();
        cached_log_lines.resize(first_line);
        cached_block_starts.resize(first_block);

        RenderedLog rendered =
            make_log_lines(log_blocks.blocks(), first_block);
        for (const std::size_t block_start : rendered.block_starts) {
            cached_block_starts.push_back(first_line + block_start);
        }
        cached_log_lines.insert(
            cached_log_lines.end(),
            std::make_move_iterator(rendered.lines.begin()),
            std::make_move_iterator(rendered.lines.end()));
        if (!log_blocks.empty()) {
            selected_block = std::min(selected_block, log_blocks.size() - 1);
        }
        log_panel_dirty = true;
    };

    auto renderer = Renderer(input, [&] {
        const TuiSnapshot snapshot = session.snapshot(cached_log_revision);
        const auto render_now = RunStatusAnimation::Clock::now();
        run_status_animation.sync(
            snapshot.running,
            snapshot.task_id,
            snapshot.activity_text,
            snapshot.turn_started_at,
            snapshot.activity_started_at);
        const Dimensions terminal_size = Terminal::Size();
        if (terminal_size.dimx != cached_terminal_width ||
            terminal_size.dimy != cached_terminal_height) {
            cached_terminal_width = terminal_size.dimx;
            cached_terminal_height = terminal_size.dimy;
            log_panel_dirty = true;
        }
        bool blocks_changed = false;
        if (snapshot.logs_changed) {
            if (snapshot.full_resync) {
                log_blocks = TuiLogBlocks{};
                cached_log_lines.clear();
                cached_block_starts.clear();
                selected_block = 0;
                log_panel_dirty = true;
            }
            // snapshot 只携带新增条目；块模型负责合并 Command/Observation。
            const auto first_changed = log_blocks.append(snapshot.new_logs);
            cached_log_revision = snapshot.log_revision;
            if (first_changed) {
                rebuild_log_lines_from(*first_changed);
                blocks_changed = true;
            }
        }

        if (log_viewport.sync(cached_log_lines.size())) {
            log_panel_dirty = true;
        }
        if (blocks_changed && active_pane == ActivePane::Scrollback &&
            selected_block < cached_block_starts.size()) {
            log_panel_dirty = log_viewport.jump_to(
                                  cached_block_starts[selected_block]) ||
                log_panel_dirty;
        }

        if (log_panel_dirty) {
            // spinner 帧会跳过此块，直接复用 cached_log_panel。
            Elements log_elements;
            if (cached_log_lines.empty()) {
                log_elements.push_back(
                    text("Enter a task below to start the agent.") | dim | center);
            } else {
                const std::size_t terminal_rows = static_cast<std::size_t>(
                    std::max(terminal_size.dimy, 1));
                const std::size_t render_limit = std::clamp<std::size_t>(
                    terminal_rows * 6,
                    120,
                    600);
                const LogWindow window =
                    log_viewport.render_window(render_limit);
                log_elements.reserve(window.end - window.begin);
                for (std::size_t i = window.begin; i < window.end; ++i) {
                    const LogLine& log_line = cached_log_lines[i];
                    Element line = log_line.heading
                        ? text("● " + log_line.text)
                        : paragraph(log_line.text);
                    if (log_line.heading) {
                        line = line | bold | color(log_color(log_line.kind));
                    } else if (log_line.kind == TuiLogKind::Final) {
                        line = line | bold;
                    } else if (log_line.kind == TuiLogKind::System) {
                        line = line | dim;
                    }
                    if (i == log_viewport.current_line()) {
                        line = line | focus;
                    }
                    if (active_pane == ActivePane::Scrollback &&
                        log_line.heading &&
                        log_line.block_index == selected_block) {
                        line = line | inverted;
                    }
                    log_elements.push_back(std::move(line));
                }
            }

            Element panel = vbox(std::move(log_elements)) |
                frame | flex;
            cached_log_panel = active_pane == ActivePane::Scrollback
                ? panel | borderStyled(Color::Cyan)
                : panel | border;
            log_panel_dirty = false;
        }

        Element input_panel;
        if (snapshot.running) {
            const bool command_activity =
                snapshot.activity_text.starts_with("Run ");
            const Color activity_color = snapshot.status == TuiStatus::Stopping
                ? Color::Red
                : command_activity ? Color::Green : Color::Cyan;
            const std::string phase_elapsed = format_run_duration(
                run_status_animation.phase_elapsed(render_now));
            const std::string turn_elapsed = format_run_duration(
                run_status_animation.turn_elapsed(render_now));
            const int inner_width = std::max(cached_terminal_width - 2, 0);
            const bool show_phase_elapsed = inner_width >= 20;

            std::string right_text;
            if (inner_width >= 32) {
                right_text = turn_elapsed + "  ";
            }
            if (inner_width >= 10) {
                right_text += "Esc stop ";
            } else if (inner_width >= 5) {
                right_text += "Esc ";
            }

            const std::string phase_text = show_phase_elapsed
                ? " " + phase_elapsed
                : std::string{};
            const int fixed_width = 3 +
                ftxui::string_width(phase_text) +
                ftxui::string_width(right_text);
            const std::string activity = truncate_to_width(
                format_run_activity(run_status_animation.activity()),
                std::max(inner_width - fixed_width, 0));

            Elements run_status_elements{
                text(" "),
                text(std::string{run_spinner_frame(animation_frame.load())}) |
                    color(activity_color) | bold,
            };
            if (!activity.empty()) {
                if (command_activity && activity.starts_with("Run ")) {
                    run_status_elements.push_back(text(" Run ") | dim);
                    run_status_elements.push_back(
                        text(activity.substr(4)) |
                        color(Color::Yellow) | bold);
                } else {
                    run_status_elements.push_back(
                        text(" " + activity) |
                        color(activity_color) | bold);
                }
            }
            if (!phase_text.empty()) {
                run_status_elements.push_back(text(phase_text) | dim);
            }
            run_status_elements.push_back(filler());
            if (!right_text.empty()) {
                run_status_elements.push_back(text(right_text) | dim);
            }
            input_panel = hbox(std::move(run_status_elements)) | border;
        } else {
            Element prompt = hbox({
                text(" ❯ ") | bold | color(Color::Cyan),
                active_pane == ActivePane::Prompt
                    ? input->Render() | flex
                    : text(task_input.empty()
                               ? "Describe a task and press Enter"
                               : task_input) |
                        flex | dim,
            });
            input_panel = active_pane == ActivePane::Prompt
                ? prompt | borderStyled(Color::Cyan)
                : prompt | border;
        }

        const Element header_status = snapshot.running
            ? text("")
            : text("● " + snapshot.status_text + " ") |
                bold | color(status_color(snapshot.status));
        const Element header = hbox({
            text(" SWE Agent") | bold | color(Color::Cyan),
            filler(),
            header_status,
        });

        const Element status_bar = hbox({
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
            text(log_viewport.following_tail()
                     ? "following latest "
                     : "scroll paused ") |
                dim,
            cached_log_lines.empty()
                ? text("")
                : text(std::to_string(log_viewport.current_line() + 1) + "/" +
                       std::to_string(cached_log_lines.size()) + " ") |
                    dim,
        });

        Elements hints;
        if (snapshot.running) {
            hints = {
                shortcut("Esc/Ctrl+C", "stop"),
                shortcut("Ctrl+D", "stop & exit"),
                shortcut("↑/↓", "scroll"),
            };
        } else {
            if (active_pane == ActivePane::Prompt) {
                hints = {
                    shortcut("Enter", "send"),
                    shortcut("↑/↓", "history"),
                    shortcut("Tab", "logs"),
                    shortcut("Ctrl+D", "exit"),
                };
            } else {
                hints = {
                    shortcut("↑/↓", "select"),
                    shortcut("Enter", "fold"),
                    shortcut("Tab", "prompt"),
                    shortcut("PgUp/PgDn", "scroll"),
                };
            }
        }
        if (!log_viewport.following_tail()) {
            hints.push_back(shortcut("End", "latest"));
        }

        Elements shortcut_row;
        for (std::size_t i = 0; i < hints.size(); ++i) {
            if (i > 0) {
                shortcut_row.push_back(text("  │  ") | dim);
            }
            shortcut_row.push_back(std::move(hints[i]));
        }

        return vbox({
            header,
            cached_log_panel,
            input_panel,
            status_bar,
            hbox(std::move(shortcut_row)),
        });
    });

    const auto exit_loop = app.ExitLoopClosure();
    const auto scroll_up = [&](std::size_t lines) {
        scroll_animation_pending.store(log_viewport.scroll_up(lines));
        animation_cv.notify_one();
    };
    const auto scroll_down = [&](std::size_t lines) {
        scroll_animation_pending.store(log_viewport.scroll_down(lines));
        animation_cv.notify_one();
    };
    const auto focus_selected_block = [&] {
        if (selected_block < cached_block_starts.size()) {
            log_panel_dirty =
                log_viewport.jump_to(cached_block_starts[selected_block]) ||
                log_panel_dirty;
            scroll_animation_pending.store(false);
        }
    };
    const auto select_previous_block = [&] {
        if (selected_block > 0) {
            --selected_block;
            log_panel_dirty = true;
            focus_selected_block();
        }
    };
    const auto select_next_block = [&] {
        if (selected_block + 1 < log_blocks.size()) {
            ++selected_block;
            log_panel_dirty = true;
            focus_selected_block();
        }
    };

    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == animation_event) {
            animation_event_queued.store(false);
            // 使用距离相关步长向目标缓动，长距离滚动不会积压大量帧。
            if (log_viewport.tick()) {
                log_panel_dirty = true;
            }
            scroll_animation_pending.store(
                log_viewport.animation_pending());
            return true;
        }

        if (event == refresh_event) {
            refresh_event_queued.store(false);
            if (exit_after_stop && !session.running()) {
                exit_loop();
            }
            return true;
        }

        if (event != Event::CtrlC) {
            idle_ctrl_c_armed = false;
        }

        if (event == Event::CtrlQ) {
            if (session.running()) {
                exit_after_stop = true;
                session.request_stop();
            } else {
                exit_loop();
            }
            return true;
        }

        if (event == Event::CtrlD) {
            if (session.running()) {
                exit_after_stop = true;
                session.request_stop();
                return true;
            }
            if (task_input.empty()) {
                exit_loop();
                return true;
            }
            return false;
        }

        if (event == Event::CtrlL) {
            app.RequestAnimationFrame();
            return true;
        }

        if (event == Event::PageUp) {
            scroll_up(5);
            return true;
        }

        if (event == Event::PageDown) {
            scroll_down(5);
            return true;
        }

        if (event.is_mouse() && event.mouse().button == Mouse::WheelUp) {
            scroll_up(3);
            return true;
        }

        if (event.is_mouse() && event.mouse().button == Mouse::WheelDown) {
            scroll_down(3);
            return true;
        }

        if (event == Event::Home) {
            log_panel_dirty = log_viewport.home() || log_panel_dirty;
            if (active_pane == ActivePane::Scrollback && !log_blocks.empty()) {
                selected_block = 0;
                log_panel_dirty = true;
            }
            scroll_animation_pending.store(false);
            return true;
        }

        if (event == Event::End) {
            log_panel_dirty = log_viewport.end() || log_panel_dirty;
            if (active_pane == ActivePane::Scrollback && !log_blocks.empty()) {
                selected_block = log_blocks.size() - 1;
                log_panel_dirty = true;
            }
            scroll_animation_pending.store(false);
            return true;
        }

        const bool running = session.running();

        if (!running &&
            (event == Event::Tab || event == Event::TabReverse)) {
            if (active_pane == ActivePane::Prompt && !log_blocks.empty()) {
                active_pane = ActivePane::Scrollback;
                const auto next = std::upper_bound(
                    cached_block_starts.begin(),
                    cached_block_starts.end(),
                    log_viewport.current_line());
                selected_block = next == cached_block_starts.begin()
                    ? 0
                    : static_cast<std::size_t>(
                          std::distance(cached_block_starts.begin(), next) - 1);
                focus_selected_block();
            } else {
                active_pane = ActivePane::Prompt;
            }
            log_panel_dirty = true;
            return true;
        }

        if (running && event == Event::ArrowUp) {
            scroll_up(1);
            return true;
        }

        if (running && event == Event::ArrowDown) {
            scroll_down(1);
            return true;
        }

        if (event == Event::ArrowUpCtrl) {
            scroll_up(1);
            return true;
        }

        if (event == Event::ArrowDownCtrl) {
            scroll_down(1);
            return true;
        }

        if (!running && active_pane == ActivePane::Scrollback &&
            event == Event::ArrowUp) {
            select_previous_block();
            return true;
        }

        if (!running && active_pane == ActivePane::Scrollback &&
            event == Event::ArrowDown) {
            select_next_block();
            return true;
        }

        if (event == Event::CtrlC) {
            if (running) {
                session.request_stop();
                return true;
            }

            if (task_input.empty() && idle_ctrl_c_armed) {
                exit_loop();
            } else {
                task_input.clear();
                prompt_history.cancel_navigation();
                idle_ctrl_c_armed = true;
            }
            return true;
        }

        if (event == Event::Escape) {
            if (running) {
                session.request_stop();
            } else if (active_pane == ActivePane::Scrollback) {
                active_pane = ActivePane::Prompt;
                log_panel_dirty = true;
            } else {
                task_input.clear();
                prompt_history.cancel_navigation();
            }
            return true;
        }

        if (!running && active_pane == ActivePane::Prompt &&
            event == Event::ArrowUp) {
            return prompt_history.previous(task_input);
        }

        if (!running && active_pane == ActivePane::Prompt &&
            event == Event::ArrowDown) {
            return prompt_history.next(task_input);
        }

        if (event == Event::Return) {
            if (!running && active_pane == ActivePane::Scrollback) {
                if (log_blocks.toggle(selected_block)) {
                    rebuild_log_lines_from(selected_block);
                    (void)log_viewport.sync(cached_log_lines.size());
                    focus_selected_block();
                }
            } else if (!running) {
                start_task();
            }
            return true;
        }

        if (!running && active_pane == ActivePane::Prompt &&
            (event.is_character() || event == Event::Backspace ||
             event == Event::Delete)) {
            prompt_history.cancel_navigation();
        }

        return running || active_pane == ActivePane::Scrollback;
    });

    try {
        animator = std::thread([&] {
            using namespace std::chrono_literals;
            std::size_t spinner_tick = 0;
            std::unique_lock animation_lock{animation_mutex};

            while (true) {
                animation_cv.wait(animation_lock, [&] {
                    return !animation_active.load() || session.running() ||
                        scroll_animation_pending.load();
                });
                if (!animation_active.load()) {
                    break;
                }

                if (animation_cv.wait_for(animation_lock, 32ms, [&] {
                        return !animation_active.load();
                    })) {
                    break;
                }
                animation_lock.unlock();

                const bool running = session.running();
                if (running) {
                    spinner_tick = (spinner_tick + 1) % 4;
                    if (spinner_tick == 0) {
                        animation_frame.fetch_add(1);
                    }
                } else {
                    spinner_tick = 0;
                }
                // Grok 风格：状态条 30fps 更新，spinner 每 4 tick 换帧。
                const bool refresh_animation =
                    running || scroll_animation_pending.load();
                if (refresh_animation &&
                    !animation_event_queued.exchange(true)) {
                    // animator 不直接修改 UI 状态，只请求主线程处理一帧。
                    try {
                        app.PostEvent(animation_event);
                    } catch (...) {
                        animation_event_queued.store(false);
                        animation_active.store(false);
                    }
                }
                animation_lock.lock();
            }
        });
        app.Loop(component);
    } catch (...) {
        animation_active.store(false);
        animation_cv.notify_one();
        if (animator.joinable()) {
            animator.join();
        }
        session.stop_and_join();
        throw;
    }

    animation_active.store(false);
    animation_cv.notify_one();
    if (animator.joinable()) {
        animator.join();
    }
    session.stop_and_join();
    return 0;
}

}  // namespace swe_agent::tui
