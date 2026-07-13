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
template <model::Provider P>
model::ModelResponse run(P& provider, const config::AgentConfig& agent_cfg) {
    model::MSG history;
    history.push_back({model::Role::System, agent_cfg.system_prompt});
    history.push_back({model::Role::User, agent_cfg.user_prompt});

    const std::size_t step_limit = agent_cfg.step_limit;
    model::ModelResponse last{};

    for (std::size_t step = 0; step < step_limit; ++step) {
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
    }

    return last;
}

}  // namespace swe_agent::agent
