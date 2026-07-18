#pragma once

#include "agent/cancellation.hpp"

#include <cstddef>
#include <functional>
#include <string>

namespace swe_agent::agent {

// AgentRunOptions::on_event 按执行顺序同步发出的过程事件。
// 终态仍以 AgentRunResult 为准，不能仅凭 Completed 判断 Worker 已返回。
enum class AgentEventType {
    Assistant,
    FormatError,
    CommandStarted,
    CommandFinished,
    Completed,
    Stopped,
    StepLimitReached,
    EmptyResponse,
};

struct AgentEvent {
    AgentEventType type;
    // 与 Agent Loop 使用同一套从 0 开始的 step 编号。
    std::size_t step{0};
    // Assistant/Observation/Final 等供消费者展示的正文。
    std::string content;
    // 仅命令相关事件使用；其它事件保持为空。
    std::string command;
};

// 回调运行在调用 agent::run() 的线程上；TUI 中即 Worker 线程。
using AgentEventHandler = std::function<void(const AgentEvent&)>;

struct AgentRunOptions {
    // 实时事件只用于展示过程；任务终态以 AgentRunResult 为准。
    AgentEventHandler on_event;
    // 协作式停止：阻塞中的 HTTP/Shell 不会被强制中断。
    StopToken stop_token;
};

}  // namespace swe_agent::agent
