#pragma once

#include "agent/process_result.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace swe_agent::agent {

// 从 assistant 文本里解析要执行的命令。
// 约定：单独一行 `RUN: <command>`（可夹在其它说明文字中）。
// 找不到则返回 nullopt（loop 侧会 nudge，不再当作收工）。
std::optional<std::string> extract_run_command(const std::string& assistant_text);

// 执行 shell 命令，合并 stdout/stderr，并返回结构化结果（不抛）。
// 注意：仅用于本机受控练习，命令来自模型输出，勿接不可信远端。
ProcessResult run_shell(const std::string& command);

/**
 * @brief 在指定工作目录执行 Shell 命令。
 *
 * 工作目录只在子进程内切换，不会修改宿主进程的全局当前目录。
 *
 * @param command 要执行的命令文本。
 * @param working_directory 子进程工作目录。
 * @return 结构化进程结果。
 */
ProcessResult run_shell(
    const std::string& command,
    const std::filesystem::path& working_directory);

// 将结构化执行结果转换成给 Agent/人阅读的 observation。
std::string format_process_result(
    const std::string& command,
    const ProcessResult& result);

}  // namespace swe_agent::agent
