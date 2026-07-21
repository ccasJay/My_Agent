#include "tui/tui.hpp"

#include "agent/session_manager.hpp"
#include "tui/log_block.hpp"
#include "tui/log_viewport.hpp"
#include "tui/prompt_history.hpp"
#include "tui/run_status.hpp"
#include "tui/session_command.hpp"
#include "tui/tui_session.hpp"
#include "tui/tui_state.hpp"
#include "tui/tui_view.hpp"

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace swe_agent::tui {

int run(
    agent::SessionManager& session_manager,
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
    bool cached_approval_pending = false;
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

    const std::filesystem::path workspace_root{session_manager.workspace()};
    TuiSession session{
        model_name,
        [&session_manager](
            const std::string& task,
            const agent::AgentRunOptions& options) {
            return session_manager.submit(task, options);
        },
        post_refresh,
        agent::PolicyContext{
            // 当前 shell 无独立 cwd；与 workspace 对齐，供后续路径规则使用。
            .working_dir = workspace_root,
            .workspace_root = workspace_root,
        },
    };
    (void)session.load_session(session_manager.active_snapshot());

    // 由 FTXUI Input 统一处理 UTF-8 编辑、光标位置和水平跟随。
    int input_cursor = 0;
    constexpr std::string_view kPromptPlaceholder =
        "Describe a task and press Enter";
    InputOption input_option;
    input_option.placeholder = std::string{kPromptPlaceholder};
    input_option.multiline = false;
    input_option.cursor_position = &input_cursor;
    // 保留 Input 内建光标，只去掉默认的整行反色样式。
    input_option.transform = [](InputState state) {
        if (state.is_placeholder) {
            state.element = state.element | dim;
        }
        return state.element;
    };
    auto input = Input(&task_input, input_option);

    auto start_task = [&] {
        const std::string task = task_input;
        const SessionCommand command = parse_session_command(task);
        if (command.kind != SessionCommandKind::None) {
            try {
                switch (command.kind) {
                case SessionCommandKind::New:
                    (void)session_manager.new_session();
                    (void)session.load_session(
                        session_manager.active_snapshot());
                    cached_log_revision =
                        std::numeric_limits<std::size_t>::max();
                    break;
                case SessionCommandKind::List: {
                    std::ostringstream content;
                    const auto sessions = session_manager.list_sessions();
                    for (const auto& summary : sessions) {
                        const std::string title = summary.title.empty()
                            ? "(untitled)"
                            : summary.title;
                        content << summary.id.substr(
                                       0,
                                       std::min<std::size_t>(8, summary.id.size()))
                                << "  " << title << "  ["
                                << summary.model_name << "]\n";
                    }
                    session.append_notice(
                        "Sessions",
                        content.str().empty()
                            ? "No sessions in this workspace."
                            : content.str());
                    break;
                }
                case SessionCommandKind::Resume:
                    (void)session_manager.resume(command.argument);
                    (void)session.load_session(
                        session_manager.active_snapshot());
                    cached_log_revision =
                        std::numeric_limits<std::size_t>::max();
                    break;
                case SessionCommandKind::Invalid:
                    session.append_notice("Session command", command.error, true);
                    break;
                case SessionCommandKind::None:
                    break;
                }
            } catch (const std::exception& error) {
                session.append_notice("Session error", error.what(), true);
            }

            task_input.clear();
            input_cursor = 0;
            idle_ctrl_c_armed = false;
            log_viewport.follow_tail();
            return;
        }

        if (!session.start(task)) {
            return;
        }

        prompt_history.record(task);
        task_input.clear();
        input_cursor = 0;
        idle_ctrl_c_armed = false;
        log_viewport.follow_tail();
        exit_after_stop = false;
        animation_cv.notify_one();
    };

    auto rebuild_log_lines_from = [&](std::size_t first_block) {
        first_block = std::min(first_block, log_blocks.size());
        const std::size_t first_line =
            first_block < cached_block_starts.size()
            ? cached_block_starts[first_block]
            : cached_log_lines.size();
        cached_log_lines.resize(first_line);
        cached_block_starts.resize(first_block);

        // 边框占两列；在进入 viewport 前生成真实可滚动的显示行。
        const int content_width = std::max(cached_terminal_width - 2, 1);
        RenderedLog rendered = make_log_lines(
            log_blocks.blocks(),
            first_block,
            content_width);
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
        if (snapshot.awaiting_approval != cached_approval_pending) {
            cached_approval_pending = snapshot.awaiting_approval;
            log_panel_dirty = true;
        }
        const auto render_now = RunStatusAnimation::Clock::now();
        run_status_animation.sync(
            snapshot.running,
            snapshot.task_id,
            snapshot.activity_text,
            snapshot.turn_started_at,
            snapshot.activity_started_at);
        const Dimensions terminal_size = Terminal::Size();
        const bool width_changed =
            terminal_size.dimx != cached_terminal_width;
        if (width_changed || terminal_size.dimy != cached_terminal_height) {
            cached_terminal_width = terminal_size.dimx;
            cached_terminal_height = terminal_size.dimy;
            log_panel_dirty = true;
            if (width_changed && !log_blocks.empty()) {
                rebuild_log_lines_from(0);
            }
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
            const std::size_t render_limit = static_cast<std::size_t>(
                std::max(
                    terminal_size.dimy -
                        (snapshot.awaiting_approval ? 10 : 8),
                    1));
            cached_log_panel = render_log_panel(
                cached_log_lines,
                log_viewport.render_window(render_limit),
                active_pane,
                selected_block);
            log_panel_dirty = false;
        }

        const Element input_panel = snapshot.awaiting_approval
            ? render_approval_panel(snapshot, cached_terminal_width)
            : snapshot.running
            ? render_run_panel(
                  snapshot,
                  run_status_animation,
                  render_now,
                  animation_frame.load(),
                  cached_terminal_width)
            : render_prompt_panel(
                  active_pane,
                  input,
                  task_input,
                  kPromptPlaceholder);

        return vbox({
            render_header(snapshot),
            cached_log_panel,
            input_panel,
            render_status_bar(
                snapshot,
                active_pane,
                log_viewport,
                cached_log_lines.size()),
            render_shortcuts(
                snapshot.running,
                snapshot.awaiting_approval,
                snapshot.command_mode,
                active_pane,
                log_viewport.following_tail()),
        });
    });

    const auto exit_loop = app.ExitLoopClosure();
    const auto sync_selected_block_to_viewport = [&] {
        if (cached_block_starts.empty()) {
            return;
        }
        const auto next = std::upper_bound(
            cached_block_starts.begin(),
            cached_block_starts.end(),
            log_viewport.current_line());
        const std::size_t block = next == cached_block_starts.begin()
            ? 0
            : static_cast<std::size_t>(
                  std::distance(cached_block_starts.begin(), next) - 1);
        if (selected_block != block) {
            selected_block = block;
            log_panel_dirty = true;
        }
    };
    const auto advance_scroll = [&] {
        // 按键当帧立即移动一次，剩余距离再由 animator 缓动。
        // 单行滚动因此没有固定 32ms 的首帧延迟。
        if (log_viewport.tick()) {
            log_panel_dirty = true;
            if (active_pane == ActivePane::Scrollback) {
                sync_selected_block_to_viewport();
            }
        }
        const bool pending = log_viewport.animation_pending();
        scroll_animation_pending.store(pending);
        if (pending) {
            animation_cv.notify_one();
        }
    };
    const auto scroll_up = [&](std::size_t lines) {
        (void)log_viewport.scroll_up(lines);
        advance_scroll();
    };
    const auto scroll_down = [&](std::size_t lines) {
        (void)log_viewport.scroll_down(lines);
        advance_scroll();
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
                if (active_pane == ActivePane::Scrollback) {
                    sync_selected_block_to_viewport();
                }
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

        if (session.awaiting_command_approval()) {
            if (event == Event::Character('y') ||
                event == Event::Character('Y')) {
                (void)session.approve_command();
                return true;
            }
            if (event == Event::Character('n') ||
                event == Event::Character('N')) {
                (void)session.reject_command();
                return true;
            }
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

        if (!running && event == Event::TabReverse) {
            (void)session.toggle_command_mode();
            return true;
        }

        if (!running && event == Event::Tab) {
            if (active_pane == ActivePane::Prompt && !log_blocks.empty()) {
                active_pane = ActivePane::Scrollback;
                sync_selected_block_to_viewport();
            } else {
                active_pane = ActivePane::Prompt;
                input->TakeFocus();
            }
            log_panel_dirty = true;
            return true;
        }

        if ((running || active_pane == ActivePane::Scrollback) &&
            event == Event::ArrowUp) {
            scroll_up(1);
            return true;
        }

        if ((running || active_pane == ActivePane::Scrollback) &&
            event == Event::ArrowDown) {
            scroll_down(1);
            return true;
        }

        if (event == Event::ArrowUpCtrl) {
            if (!running && active_pane == ActivePane::Scrollback) {
                select_previous_block();
            } else {
                scroll_up(1);
            }
            return true;
        }

        if (event == Event::ArrowDownCtrl) {
            if (!running && active_pane == ActivePane::Scrollback) {
                select_next_block();
            } else {
                scroll_down(1);
            }
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
                input_cursor = 0;
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
                input->TakeFocus();
                log_panel_dirty = true;
            } else {
                task_input.clear();
                input_cursor = 0;
                prompt_history.cancel_navigation();
            }
            return true;
        }

        if (!running && active_pane == ActivePane::Prompt &&
            event == Event::ArrowUp) {
            if (!prompt_history.previous(task_input)) {
                return false;
            }
            input_cursor = static_cast<int>(task_input.size());
            return true;
        }

        if (!running && active_pane == ActivePane::Prompt &&
            event == Event::ArrowDown) {
            if (!prompt_history.next(task_input)) {
                return false;
            }
            input_cursor = static_cast<int>(task_input.size());
            return true;
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
