#pragma once

#include "agent/agent_event.hpp"
#include "agent/agent_run_result.hpp"
#include "tui/tui_state.hpp"

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace swe_agent::tui {

// UI 的一次一致性只读快照。new_logs 是 known_log_revision 之后的增量，
// 避免每次动画或事件刷新都复制完整日志历史。
struct TuiSnapshot {
    std::string status_text;
    std::string activity_text;
    std::string model_name;
    TuiStatus status{TuiStatus::Ready};
    std::size_t step{0};
    bool running{false};
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

    TuiSession(std::string model_name, TaskRunner runner, Notify notify);
    ~TuiSession();

    TuiSession(const TuiSession&) = delete;
    TuiSession& operator=(const TuiSession&) = delete;

    // 同一时间只允许一个任务；运行中或任务无效时返回 false。
    bool start(std::string task);
    // 只发出协作式停止请求，不会在此处阻塞等待 Worker。
    bool request_stop();
    // 用于退出/析构：请求停止并完整回收 Worker。
    void stop_and_join();

    [[nodiscard]] bool running() const;
    [[nodiscard]] TuiSnapshot snapshot(
        std::size_t known_log_revision) const;

private:
    void join_worker();
    void notify() noexcept;

    // state_ 的所有跨线程访问都必须持有 mutex_。
    mutable std::mutex mutex_;
    TuiState state_;
    TaskRunner runner_;
    Notify notify_;
    std::thread worker_;
    agent::StopSource stop_source_;
};

}  // namespace swe_agent::tui
