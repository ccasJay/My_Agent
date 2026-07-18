#include "tui/tui_session.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace swe_agent::tui {

TuiSession::TuiSession(
    std::string model_name,
    TaskRunner runner,
    Notify notify_callback)
    : state_(std::move(model_name)),
      runner_(std::move(runner)),
      notify_(std::move(notify_callback)) {
    if (!runner_) {
        throw std::invalid_argument{"TuiSession requires a task runner"};
    }
}

TuiSession::~TuiSession() {
    stop_and_join();
}

bool TuiSession::start(std::string task) {
    {
        std::lock_guard lock{mutex_};
        if (state_.running()) {
            return false;
        }
    }

    // 上个任务已结束，但 std::thread 对象仍需显式回收后才能复用。
    join_worker();

    {
        std::lock_guard lock{mutex_};
        if (!state_.begin_task(task)) {
            return false;
        }
    }

    stop_source_ = agent::StopSource{};
    const agent::StopToken stop_token = stop_source_.token();
    try {
        worker_ = std::thread([this, task = std::move(task), stop_token] {
            agent::AgentRunOptions options;
            options.stop_token = stop_token;
            options.on_event = [this](const agent::AgentEvent& event) {
                {
                    std::lock_guard lock{mutex_};
                    state_.apply_event(event);
                }
                notify();
            };

            try {
                const agent::AgentRunResult result = runner_(task, options);
                {
                    std::lock_guard lock{mutex_};
                    // 只有 runner 真正返回后才结束 busy，防止新旧任务 Worker 重叠。
                    state_.apply_result(result);
                }
            } catch (const std::exception& error) {
                std::lock_guard lock{mutex_};
                state_.fail_task(error.what());
            } catch (...) {
                std::lock_guard lock{mutex_};
                state_.fail_task("Unknown error");
            }
            notify();
        });
    } catch (...) {
        {
            std::lock_guard lock{mutex_};
            state_.fail_task("Unable to start agent worker");
        }
        notify();
        throw;
    }

    notify();
    return true;
}

bool TuiSession::request_stop() {
    bool accepted = false;
    {
        std::lock_guard lock{mutex_};
        accepted = state_.request_stop();
    }
    if (accepted) {
        stop_source_.request_stop();
        notify();
    }
    return accepted;
}

void TuiSession::stop_and_join() {
    stop_source_.request_stop();
    join_worker();
}

bool TuiSession::running() const {
    std::lock_guard lock{mutex_};
    return state_.running();
}

TuiSnapshot TuiSession::snapshot(std::size_t known_log_revision) const {
    std::lock_guard lock{mutex_};
    const std::size_t log_revision = state_.log_revision();
    const bool full_resync = known_log_revision > log_revision;
    TuiSnapshot snapshot{
        .status_text = state_.status_text(),
        .activity_text = state_.activity_text(),
        .model_name = state_.model_name(),
        .status = state_.status(),
        .step = state_.step(),
        .running = state_.running(),
        .task_id = state_.task_id(),
        .turn_started_at = state_.turn_started_at(),
        .activity_started_at = state_.activity_started_at(),
        .log_revision = log_revision,
        .logs_changed = log_revision != known_log_revision,
        .full_resync = full_resync,
    };
    if (snapshot.logs_changed) {
        const auto& logs = state_.logs();
        // revision 与 append 次数一致。若消费者传入未知 revision，则全量重同步。
        const std::size_t first_new_log = full_resync ? 0 : known_log_revision;
        snapshot.new_logs.assign(
            logs.begin() + static_cast<std::ptrdiff_t>(first_new_log),
            logs.end());
    }
    return snapshot;
}

void TuiSession::join_worker() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

void TuiSession::notify() noexcept {
    try {
        if (notify_) {
            notify_();
        }
    } catch (...) {
        // UI 已退出或无法接收刷新时，worker 仍应正常完成和回收。
    }
}

}  // namespace swe_agent::tui
