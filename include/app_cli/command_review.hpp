#pragma once

#include "agent/agent_event.hpp"

#include <iosfwd>

namespace swe_agent::app_cli {

/**
 * @brief 在 Console 中请求用户审核命令。
 *
 * 该函数只依赖传入的流与交互状态，便于单元测试；非交互输入或输入结束会安全地拒绝命令。
 *
 * @param request 需要人工审核的命令。
 * @param input 用于读取用户回答的输入流。
 * @param output 用于输出中文提示的输出流。
 * @param interactive 标准输入是否为 TTY。
 * @return 用户的批准或拒绝决定。
 */
agent::CommandDecision review_console_command(
    const agent::CommandRequest& request,
    std::istream& input,
    std::ostream& output,
    bool interactive);

}  // namespace swe_agent::app_cli
