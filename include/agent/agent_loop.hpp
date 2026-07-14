#pragma once

#include "agent/shell.hpp"
#include "config/agent_loader.hpp"
#include "model/message.hpp"
#include "model/model.hpp"

#include <cstddef>
#include <iostream>
#include <string>

namespace swe_agent::agent {

// 依赖 Provider 契约，不绑死具体实现（如 OpenaiCompatible）
/**
 * @brief Agent loop
 * 
 * @tparam P 
 * @param provider 
 * @param agent_cfg 
 * @return model::ModelResponse 
 * @note 初始history压入agent_cfg中的prompt -> 创建一个last{}用于记录对话 -> 进入loop: 
 *  [模型本轮输出先进 history -> 从assistant文本中解析要执行的命令 -> run_shell()解析命令，记录为observation ,有才进入下一轮-> 将user + observation 压入history进行下一轮] 
 */
template <model::Provider P>
model::ModelResponse run(P& provider, const config::AgentConfig& agent_cfg) {
    model::MSG history;
    history.push_back({model::Role::System, agent_cfg.system_prompt});
    history.push_back({model::Role::User, agent_cfg.user_prompt});

    const std::size_t step_limit = agent_cfg.step_limit;
    model::ModelResponse last{};
    std::size_t step = 0;

    while (true) {
        if(agent_cfg.step_limit > 0 && step >= agent_cfg.step_limit) {
            break;
        }

        last = provider.query(history);
        if (last.content.empty()) {
            break;
        }

        // 每轮 assistant 都打印（不只最后一轮）
        std::cout << "----- step " << step << " (assistant) -----\n"
                  << last.content << '\n';

        // 1) 模型本轮输出先进 history
        history.push_back({model::Role::Assistant, last.content});

        // 2) 若含 RUN: 行 → 本地执行 → observation 以 User 写回
        const auto cmd = extract_run_command(last.content);
        if (!cmd) {
            // 无工具调用：视为最终回答，结束 loop
            break;
        }

        const std::string observation = run_shell(*cmd);
        std::cout << "----- step " << step << " (observation) -----\n"
                  << observation << '\n';

        // 前缀方便模型/人阅读；Role::User 兼容未接 tool_calls 的 API
        history.push_back({
            model::Role::User,
            std::string{"Observation:\n"} + observation,
        });
        // 有 observation 才继续下一轮 query（用掉 step）
        step++;
    }

    return last;
}

}  // namespace swe_agent::agent
