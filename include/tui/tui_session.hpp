#pragma once

#include "agent/agent_event.hpp"
#include "agent/agent_run_result.hpp"
#include "agent/command_authorization.hpp"
#include "tui/tui_state.hpp"

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace swe_agent::tui {

enum class CommandMode {
    Auto,
    Review,
};

[[nodiscard]] std::string_view command_mode_name(
    CommandMode mode) noexcept;

// UI 的一次一致性只读快照。new_logs 是 known_log_revision 之后的增量，
// 避免每次动画或事件刷新都复制完整日志历史。
struct TuiSnapshot {
    std::string status_text;
    std::string activity_text;
    std::string model_name;
    TuiStatus status{TuiStatus::Ready};
    std::size_t step{0};
    bool running{false};
    bool awaiting_approval{false};
    std::string pending_command;
    CommandMode command_mode{CommandMode::Review};
    std::size_t task_id{0};
    TuiState::TimePoint turn_started_at{};
    TuiState::TimePoint activity_started_at{};
    std::size_t log_revision{0};
    bool logs_changed{false};
    // known_log_revision 不属于当前历史时，new_logs 包含完整历史。
    bool full_resync{false};
    std::vector<TuiLogEntry> new_logs;
};

class TuiSession {
public:
    // TaskRunner 始终在 Worker 线程执行。
    using TaskRunner =
        std::function<agent::AgentRunResult(
            const std::string&,
            const agent::AgentRunOptions&)>;
    // Notify 可能从 Worker 调用，必须使用线程安全的刷新机制。
    using Notify = std::function<void()>;

    TuiSession(
        std::string model_name,
        TaskRunner runner,
        Notify notify,
        agent::PolicyContext policy_context,
        CommandMode command_mode = CommandMode::Review);
    ~TuiSession();

    TuiSession(const TuiSession&) = delete;
    TuiSession& operator=(const TuiSession&) = delete;

    // 同一时间只允许一个任务；运行中或任务无效时返回 false。
    bool start(std::string task);
    // 只发出协作式停止请求，不会在此处阻塞等待 Worker。
    bool request_stop();
    // 提交当前待审批命令的决定；无待审批命令时返回 false。
    bool approve_command();
    bool reject_command(std::string reason = "The user rejected this command.");
    // 运行中不允许切换，避免同一任务使用两套授权语义。
    bool toggle_command_mode();
    // Session switching is only valid while no task is running.
    bool load_session(const agent::SessionSnapshot& snapshot);
    void append_notice(
        std::string heading,
        std::string content,
        bool error = false);
    // 用于退出/析构：请求停止并完整回收 Worker。
    void stop_and_join();

    [[nodiscard]] bool running() const;
    [[nodiscard]] bool awaiting_command_approval() const;
    [[nodiscard]] CommandMode command_mode() const;
    [[nodiscard]] TuiSnapshot snapshot(
        std::size_t known_log_revision) const;

private:
    agent::CommandDecision authorize_command(
        const agent::CommandRequest& request,
        const agent::StopToken& stop_token);
    /**
     * @brief 通过条件变量等待 TUI 用户对命令作出决定。
     *
     * @param request 需要展示给用户的命令。
     * @param stop_token 用于在退出时解除等待的停止令牌。
     * @return 用户的批准、拒绝或停止决定。
     */
    agent::CommandDecision review_command(
        const agent::CommandRequest& request,
        const agent::StopToken& stop_token);
    bool submit_command_decision(agent::CommandDecision decision);
    void join_worker();
    void notify() noexcept;

    // state_ 的所有跨线程访问都必须持有 mutex_。
    mutable std::mutex mutex_;
    std::condition_variable approval_cv_;
    TuiState state_;
    TaskRunner runner_;
    Notify notify_;
    std::thread worker_;
    agent::StopSource stop_source_;
    std::optional<agent::CommandDecision> approval_decision_;
    CommandMode command_mode_{CommandMode::Review};
    // 构造时注入，进程内不变；authorize_command 只读，不必进 mutex_。
    agent::PolicyContext policy_context_;
};

}  // namespace swe_agent::tui
