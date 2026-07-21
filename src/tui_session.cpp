#include "tui/tui_session.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace swe_agent::tui {

std::string_view command_mode_name(CommandMode mode) noexcept {
    switch (mode) {
    case CommandMode::Auto:
        return "auto";
    case CommandMode::Review:
        return "review";
    }
    return "unknown";
}

TuiSession::TuiSession(
    std::string model_name,
    TaskRunner runner,
    Notify notify_callback,
    agent::PolicyContext policy_context,
    CommandMode command_mode)
    : state_(std::move(model_name)),
      runner_(std::move(runner)),
      notify_(std::move(notify_callback)),
      command_mode_(command_mode),
      policy_context_(std::move(policy_context)) {
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
        approval_decision_.reset();
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
            options.authorizer = [this, stop_token](
                                     const agent::CommandRequest& request) {
                return authorize_command(request, stop_token);
            };
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
        approval_cv_.notify_all();
        notify();
    }
    return accepted;
}

bool TuiSession::approve_command() {
    return submit_command_decision(agent::CommandDecision{
        .action = agent::CommandAction::Approve,
    });
}

bool TuiSession::reject_command(std::string reason) {
    return submit_command_decision(agent::CommandDecision{
        .action = agent::CommandAction::Reject,
        .reason = std::move(reason),
    });
}

bool TuiSession::toggle_command_mode() {
    {
        std::lock_guard lock{mutex_};
        if (state_.running()) {
            return false;
        }
        command_mode_ = command_mode_ == CommandMode::Auto
            ? CommandMode::Review
            : CommandMode::Auto;
    }
    notify();
    return true;
}

bool TuiSession::load_session(const agent::SessionSnapshot& snapshot) {
    {
        std::lock_guard lock{mutex_};
        if (state_.running()) {
            return false;
        }
    }
    join_worker();
    {
        std::lock_guard lock{mutex_};
        approval_decision_.reset();
        state_.load_session(snapshot);
    }
    notify();
    return true;
}

void TuiSession::append_notice(
    std::string heading,
    std::string content,
    bool error) {
    {
        std::lock_guard lock{mutex_};
        state_.append_notice(
            std::move(heading),
            std::move(content),
            error);
    }
    notify();
}

void TuiSession::stop_and_join() {
    stop_source_.request_stop();
    approval_cv_.notify_all();
    join_worker();
}

bool TuiSession::running() const {
    std::lock_guard lock{mutex_};
    return state_.running();
}

bool TuiSession::awaiting_command_approval() const {
    std::lock_guard lock{mutex_};
    return state_.awaiting_command_approval();
}

CommandMode TuiSession::command_mode() const {
    std::lock_guard lock{mutex_};
    return command_mode_;
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
        .awaiting_approval = state_.awaiting_command_approval(),
        .pending_command = state_.pending_command(),
        .command_mode = command_mode_,
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

agent::CommandDecision TuiSession::authorize_command(
    const agent::CommandRequest& request,
    const agent::StopToken& stop_token) {
    {
        std::lock_guard lock{mutex_};
        if (stop_token.stop_requested()) {
            return {
                .action = agent::CommandAction::Stop,
                .reason = "Stop requested",
            };
        }
    }

    // policy_context_ 只读且构造后不变，可在锁外评估。
    const agent::PolicyResult policy =
        agent::evaluate_command_policy(request.command, policy_context_);
    if (policy.action == agent::PolicyAction::Deny) {
        return {
            .action = agent::CommandAction::Reject,
            .reason = policy.reason.empty()
                ? std::string{"Denied by command policy"}
                : policy.reason,
        };
    }

    {
        std::lock_guard lock{mutex_};
        if (stop_token.stop_requested()) {
            return {
                .action = agent::CommandAction::Stop,
                .reason = "Stop requested",
            };
        }
        // Auto 仅在 policy Allow 时直批；RequireReview 仍走人工审批。
        if (command_mode_ == CommandMode::Auto &&
            policy.action == agent::PolicyAction::Allow) {
            return {
                .action = agent::CommandAction::Approve,
            };
        }
        approval_decision_.reset();
        state_.begin_command_approval(request);
    }
    notify();

    std::unique_lock lock{mutex_};
    approval_cv_.wait(lock, [&] {
        return approval_decision_.has_value() ||
            stop_token.stop_requested();
    });

    agent::CommandDecision decision;
    if (stop_token.stop_requested()) {
        decision = {
            .action = agent::CommandAction::Stop,
            .reason = "Stop requested",
        };
    } else {
        decision = std::move(*approval_decision_);
    }
    approval_decision_.reset();
    state_.resolve_command_approval(decision);
    lock.unlock();
    notify();
    return decision;
}

bool TuiSession::submit_command_decision(
    agent::CommandDecision decision) {
    {
        std::lock_guard lock{mutex_};
        if (!state_.awaiting_command_approval() ||
            approval_decision_.has_value()) {
            return false;
        }
        approval_decision_ = std::move(decision);
    }
    approval_cv_.notify_one();
    return true;
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
