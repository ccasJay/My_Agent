#pragma once

#include "cancellation.hpp"
#include "agent/process_result.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

namespace swe_agent::agent {

/**
 * @brief Agent Loop 按执行顺序同步发出的过程事件类型。
 *
 * 终态仍以 AgentRunResult 为准，不能仅凭 Completed 判断 Worker 已返回。
 */
enum class AgentEventType {
    Assistant,
    FormatError,
    CommandStarted,
    CommandFinished,
    CommandRejected,
    Completed,
    Stopped,
    StepLimitReached,
    EmptyResponse,
};

/** @brief 供 Console 和 TUI 渲染的 Agent 过程事件。 */
struct AgentEvent {
    AgentEventType type;
    // 与 Agent Loop 使用同一套从 0 开始的 step 编号。
    std::size_t step{0};
    // Assistant/Observation/Final 等供消费者展示的正文。
    std::string content;
    // 仅命令相关事件使用；其它事件保持为空。
    std::string command;
    // 仅命令拒绝事件使用；其它事件保持为空。
    std::string rule_id;
    // 仅 CommandFinished 使用；true 表示子进程成功退出。
    std::optional<bool> command_succeeded;
};

// 回调运行在调用 agent::run() 的线程上；TUI 中即 Worker 线程。
using AgentEventHandler = std::function<void(const AgentEvent&)>;

enum class CommandAction {
    Approve,
    Reject,
    Stop,
};

/** @brief 前端授权器收到的待执行命令。 */
struct CommandRequest {
    std::size_t step;
    std::string command;
};

/** @brief 授权器对待执行命令给出的最终决定。 */
struct CommandDecision {
    CommandAction action;
    std::string rule_id;
    std::string reason;
};

using CommandAuthorizer = std::function<CommandDecision (const CommandRequest&)>;

/** @brief Agent Loop 使用的可注入 Shell 执行器。 */
using ShellExecutor = std::function<ProcessResult(const std::string&)>;

struct AgentRunOptions {
    // 实时事件只用于展示过程；任务终态以 AgentRunResult 为准。
    AgentEventHandler on_event;
    // 协作式停止：阻塞中的 HTTP/Shell 不会被强制中断。
    StopToken stop_token;
    // 命令授权器。
    CommandAuthorizer authorizer;
    // 可选 Shell 执行器；未提供时使用当前进程工作目录。
    ShellExecutor shell_executor;
};
}  // namespace swe_agent::agent
