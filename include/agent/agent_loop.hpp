#pragma once

#include "agent/shell.hpp"
#include "config/agent_loader.hpp"
#include "model/message.hpp"
#include "model/model.hpp"

#include <cctype>
#include <cstddef>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace swe_agent::agent {
namespace {

/**
 * @brief 是否为任务完成约定命令（与 agent.yaml 中 COMPLETE_TASK 协议一致）
 *
 * 模型完成工作后应发出：RUN: echo COMPLETE_TASK
 * 主机在此识别并结束 loop，不再进入下一轮 query。
 */
bool is_task_completed(std::string_view cmd) {
    while (!cmd.empty() && (cmd.front() == ' ' || cmd.front() == '\t')) {
        cmd.remove_prefix(1);
    }
    while (!cmd.empty() && (cmd.back() == ' ' || cmd.back() == '\t')) {
        cmd.remove_suffix(1);
    }
    return cmd == "echo COMPLETE_TASK";
}

bool is_run_line(std::string_view line) {
    std::size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    return line.substr(i).starts_with("RUN:");
}

/**
 * @brief 去掉所有 RUN: 行，得到给人看的结论正文
 */
std::string strip_run_lines(const std::string& assistant_text) {
    std::istringstream in(assistant_text);
    std::string line;
    std::string out;
    while (std::getline(in, line)) {
        if (is_run_line(line)) {
            continue;
        }
        out += line;
        out.push_back('\n');
    }
    return out;
}

bool has_nonempty_conclusion(std::string_view text) {
    for (unsigned char c : text) {
        if (!std::isspace(c)) {
            return true;
        }
    }
    return false;
}

constexpr std::string_view kFormatHint =
    "Host: no valid RUN: line found.\n"
    "If work remains: RUN: <command>\n"
    "If finished: write a short Conclusion, then RUN: echo COMPLETE_TASK\n"
    "Do not reply with plain text only.";

constexpr std::string_view kMissingConclusionHint =
    "Host: COMPLETE_TASK rejected — no conclusion text in the same message.\n"
    "Write a short Conclusion (what you found), then end with:\n"
    "RUN: echo COMPLETE_TASK";

}  // namespace

// 依赖 Provider 契约，不绑死具体实现（如 OpenaiCompatible）
/**
 * @brief Agent loop
 *
 * @tparam P
 * @param provider
 * @param agent_cfg
 * @return model::ModelResponse
 * @note 初始history压入agent_cfg中的prompt -> last{} -> loop:
 *  [assistant 进 history -> 解析 RUN: -> COMPLETE_TASK 且有结论则结束;
 *   无 RUN: 则 nudge; 否则 run_shell → observation 压入 history 继续]
 */
template <model::Provider P>
model::ModelResponse run(P& provider, const config::AgentConfig& agent_cfg) {
    model::MSG history;
    history.push_back({model::Role::System, agent_cfg.system_prompt});
    history.push_back({model::Role::User, agent_cfg.user_prompt});

    model::ModelResponse last{};
    std::size_t step = 0;

    while (true) {
        if (agent_cfg.step_limit > 0 && step >= agent_cfg.step_limit) {
            break;
        }

        last = provider.query(history);
        if (last.content.empty()) {
            break;
        }

        // 1) 模型本轮输出先进 history（日志延后：成功 COMPLETE 只打 final，避免与 assistant 重复）
        history.push_back({model::Role::Assistant, last.content});

        // 2) 若含 RUN: 行 → 本地执行 → observation 以 User 写回
        const auto cmd = extract_run_command(last.content);
        if (!cmd) {
            std::cout << "================= step " << step << " (assistant) =================== \n"
                      << last.content << '\n';
            history.push_back({model::Role::User, std::string{kFormatHint}});
            std::cout << "================= step " << step
                      << " (format error, continue) =================== \n"
                      << kFormatHint << '\n';
            step++;
            continue;
        }

        // 3) 完成信号：同轮必须有非空结论（去掉 RUN: 行后），再执行 COMPLETE 并退出
        if (is_task_completed(*cmd)) {
            const std::string conclusion = strip_run_lines(last.content);
            if (!has_nonempty_conclusion(conclusion)) {
                std::cout << "================= step " << step << " (assistant) =================== \n"
                          << last.content << '\n';
                history.push_back({model::Role::User, std::string{kMissingConclusionHint}});
                std::cout << "================= step " << step
                          << " (missing conclusion, continue) =================== \n"
                          << kMissingConclusionHint << '\n';
                step++;
                continue;
            }

            // 成功收工：只打印 final（不再打整段 assistant，避免结论重复）
            std::cout << "================= final =================== \n"
                      << conclusion;
            if (conclusion.empty() || conclusion.back() != '\n') {
                std::cout << '\n';
            }

            const ProcessResult process_result = run_shell(*cmd);
            const std::string observation = format_process_result(*cmd, process_result);
            std::cout << "================= task complete =================== \n"
                      << observation << '\n';
            break;
        }

        std::cout << "================= step " << step << " (assistant) =================== \n"
                  << last.content << '\n';

        const ProcessResult process_result = run_shell(*cmd);
        const std::string observation = format_process_result(*cmd, process_result);
        std::cout << "================= step " << step << " (observation) =================== \n"
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
