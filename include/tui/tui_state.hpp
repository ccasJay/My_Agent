#pragma once

#include "agent/agent_event.hpp"
#include "agent/agent_run_result.hpp"

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace swe_agent::tui {

// 面向用户的任务生命周期状态。
enum class TuiStatus {
    Ready,
    Running,
    Stopping,
    Stopped,
    StepLimitReached,
    EmptyResponse,
    Error,
};

// Running 内部的细分活动，仅用于动态状态文案。
enum class TuiActivity {
    Idle,
    Thinking,
    RunningCommand,
};

// 日志语义决定颜色和强调样式，不向核心 Agent 暴露 FTXUI 类型。
enum class TuiLogKind {
    Task,
    Assistant,
    Command,
    Observation,
    Final,
    System,
    Error,
};

struct TuiLogEntry {
    TuiLogKind kind{TuiLogKind::System};
    std::string heading;
    std::string content;
};

// 纯状态机，本身不加锁；跨线程同步由 TuiSession 负责。
class TuiState {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit TuiState(std::string model_name);

    bool begin_task(std::string_view task);
    bool request_stop();
    // 事件只更新过程展示；不得在此提前结束 running 状态。
    void apply_event(const agent::AgentEvent& event);
    // Worker 返回后，用最终结果提交真正的终态。
    void apply_result(const agent::AgentRunResult& result);
    void fail_task(std::string message);

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] std::size_t step() const noexcept;
    [[nodiscard]] TuiStatus status() const noexcept;
    [[nodiscard]] std::size_t log_revision() const noexcept;
    [[nodiscard]] std::size_t task_id() const noexcept;
    [[nodiscard]] TimePoint turn_started_at() const noexcept;
    [[nodiscard]] TimePoint activity_started_at() const noexcept;
    [[nodiscard]] const std::string& model_name() const noexcept;
    [[nodiscard]] const std::vector<TuiLogEntry>& logs() const noexcept;
    [[nodiscard]] std::string status_text() const;
    [[nodiscard]] std::string activity_text() const;

private:
    void append(
        TuiLogKind kind,
        std::string heading,
        std::string content = {});

    std::string model_name_;
    std::vector<TuiLogEntry> logs_;
    std::size_t task_count_{0};
    std::size_t step_{0};
    std::size_t log_revision_{0};
    TuiStatus status_{TuiStatus::Ready};
    TuiActivity activity_{TuiActivity::Idle};
    std::string activity_detail_;
    TimePoint turn_started_at_{};
    TimePoint activity_started_at_{};
};

}  // namespace swe_agent::tui
