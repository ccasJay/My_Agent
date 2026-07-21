#pragma once

#include "agent/agent_event.hpp"
#include "agent/command_policy.hpp"

namespace swe_agent::agent {

/**
 * @brief 根据策略结果和前端审核器生成最终命令决定。
 *
 * 策略拒绝不可被审核器覆盖；需要审核但没有审核器时会安全地拒绝。
 *
 * @param request 待执行命令及其所属步骤。
 * @param context 命令策略使用的工作区上下文。
 * @param review_all 是否连策略允许的命令也要求人工审核。
 * @param reviewer 可选的前端审核回调。
 * @return 包含规则标识和中文原因的最终决定。
 */
CommandDecision authorize_command(
    const CommandRequest& request,
    const PolicyContext& context,
    bool review_all,
    const CommandAuthorizer& reviewer);

}  // namespace swe_agent::agent
