#pragma once

#include "agent/agent_event.hpp"
#include "agent/agent_run_result.hpp"
#include "agent/shell.hpp"
#include "config/agent_loader.hpp"
#include "model/message.hpp"
#include "model/model.hpp"

#include <cctype>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <utility>

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

void emit_event(
    const AgentRunOptions& options,
    AgentEventType type,
    std::size_t step,
    std::string content = {},
    std::string command = {}) {
    if (options.on_event) {
        options.on_event(AgentEvent{
            .type = type,
            .step = step,
            .content = std::move(content),
            .command = std::move(command),
        });
    }
}

bool should_stop(const AgentRunOptions& options) {
    return options.stop_token.stop_requested();
}

/**
 * @brief 请求授权器对命令进行审批
 *
 * @param options
 * @param step
 * @param command
 * @return CommandDecision
 */
CommandDecision request_authorization(
    const AgentRunOptions& options,
    std::size_t step,
    std::string_view command) {
    if (should_stop(options)) {
        return CommandDecision{
            .action = CommandAction::Stop,
            .reason = "Stop requested",
        };
    }

    if (!options.authorizer) {
        return CommandDecision{
            .action = CommandAction::Approve,
        };
    }
    CommandRequest request{
        .step = step,
        .command = std::string{command},
    };
    return options.authorizer(request);
}

}  // namespace

// 依赖 Provider 契约，不绑死具体实现（如 OpenaiCompatible）
/**
 * @brief Agent loop
 *
 * @tparam P
 * @param provider
 * @param agent_cfg
 * @param history 调用方持有的会话历史；循环会将模型输出和 observation 追加到其中
 * @return AgentRunResult
 * @note 初始history压入agent_cfg中的prompt -> last{} -> loop:
 *  [assistant 进 history -> 解析 RUN: -> COMPLETE_TASK 且有结论则结束;
 *   无 RUN: 则 nudge; 否则 run_shell → observation 压入 history 继续]
 */
template <model::Provider P>
AgentRunResult run(
    P& provider,
    const config::AgentConfig& agent_cfg,
    model::MSG& history,
    const AgentRunOptions& options = {}) {
    model::ModelResponse last{};
    std::size_t step = 0;
    AgentRunStatus status = AgentRunStatus::EmptyResponse;

    while (true) {
        if (should_stop(options)) {
            emit_event(options, AgentEventType::Stopped, step);
            status = AgentRunStatus::Stopped;
            break;
        }

        if (agent_cfg.step_limit > 0 && step >= agent_cfg.step_limit) {
            emit_event(options, AgentEventType::StepLimitReached, step);
            status = AgentRunStatus::StepLimitReached;
            break;
        }

        last = provider.query(history);
        if (should_stop(options)) {
            emit_event(options, AgentEventType::Stopped, step);
            status = AgentRunStatus::Stopped;
            break;
        }
        if (last.content.empty()) {
            emit_event(options, AgentEventType::EmptyResponse, step);
            status = AgentRunStatus::EmptyResponse;
            break;
        }

        // 1) 模型本轮输出先进 history（日志延后：成功 COMPLETE 只打 final，避免与 assistant 重复）
        history.push_back({model::Role::Assistant, last.content});

        // 2) 若含 RUN: 行 → 本地执行 → observation 以 User 写回
        const auto cmd = extract_run_command(last.content);
        if (!cmd) {
            emit_event(options, AgentEventType::Assistant, step, last.content);
            history.push_back({model::Role::User, std::string{kFormatHint}});
            emit_event(
                options,
                AgentEventType::FormatError,
                step,
                std::string{kFormatHint});
            step++;
            continue;
        }

        // 3) 完成信号：同轮必须有非空结论（去掉 RUN: 行后），再执行 COMPLETE 并退出
        if (is_task_completed(*cmd)) {
            const std::string conclusion = strip_run_lines(last.content);
            if (!has_nonempty_conclusion(conclusion)) {
                emit_event(options, AgentEventType::Assistant, step, last.content);
                history.push_back({model::Role::User, std::string{kMissingConclusionHint}});
                emit_event(
                    options,
                    AgentEventType::FormatError,
                    step,
                    std::string{kMissingConclusionHint});
                step++;
                continue;
            }

            if (should_stop(options)) {
                emit_event(options, AgentEventType::Stopped, step);
                status = AgentRunStatus::Stopped;
                break;
            }
            emit_event(
                options,
                AgentEventType::CommandStarted,
                step,
                {},
                *cmd);
            const ProcessResult process_result = run_shell(*cmd);
            const std::string observation = format_process_result(*cmd, process_result);
            emit_event(
                options,
                AgentEventType::CommandFinished,
                step,
                observation,
                *cmd);
            // 停止可能在 Shell 阻塞期间到达；此时 Stopped 必须优先于 Completed。
            if (should_stop(options)) {
                emit_event(options, AgentEventType::Stopped, step);
                status = AgentRunStatus::Stopped;
                break;
            }
            emit_event(options, AgentEventType::Completed, step, conclusion);
            status = AgentRunStatus::Completed;
            break;
        }

        emit_event(options, AgentEventType::Assistant, step, last.content);

        if (should_stop(options)) {
            emit_event(options, AgentEventType::Stopped, step);
            status = AgentRunStatus::Stopped;
            break;
        }

        // 只有真实 Shell 命令需要授权；COMPLETE_TASK 是内部完成协议。
        const CommandDecision decision =
            request_authorization(options, step, *cmd);
        if (decision.action == CommandAction::Stop) {
            emit_event(options, AgentEventType::Stopped, step);
            status = AgentRunStatus::Stopped;
            break;
        }

        if (decision.action == CommandAction::Reject) {
            std::string rejection = "Host: command rejected by user.\n";
            if (!decision.reason.empty()) {
                rejection += "Reason: " + decision.reason + '\n';
            }
            history.push_back({
                model::Role::User,
                std::string{"Observation:\n"} + rejection,
            });
            ++step;
            continue;
        }

        emit_event(
            options,
            AgentEventType::CommandStarted,
            step,
            {},
            *cmd);
        const ProcessResult process_result = run_shell(*cmd);
        const std::string observation = format_process_result(*cmd, process_result);
        emit_event(
            options,
            AgentEventType::CommandFinished,
            step,
            observation,
            *cmd);

        // 前缀方便模型/人阅读；Role::User 兼容未接 tool_calls 的 API
        history.push_back({
            model::Role::User,
            std::string{"Observation:\n"} + observation,
        });
        // 有 observation 才继续下一轮 query（用掉 step）
        step++;

        if (should_stop(options)) {
            emit_event(options, AgentEventType::Stopped, step);
            status = AgentRunStatus::Stopped;
            break;
        }
    }

    return AgentRunResult{
        .status = status,
        .response = std::move(last),
        .step = step,
    };
}

// 兼容一次性 Console 调用：由此重载创建初始历史，再交给可复用的核心循环。
template <model::Provider P>
AgentRunResult run(
    P& provider,
    const config::AgentConfig& agent_cfg,
    const AgentRunOptions& options = {}) {
    model::MSG history;
    history.push_back({model::Role::System, agent_cfg.system_prompt});
    history.push_back({model::Role::User, agent_cfg.user_prompt});
    return run(provider, agent_cfg, history, options);
}

}  // namespace swe_agent::agent
