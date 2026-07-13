#pragma once

#include <optional>
#include <string>

namespace swe_agent::agent {

// 从 assistant 文本里解析要执行的命令。
// 约定：单独一行 `RUN: <command>`（可夹在其它说明文字中）。
// 找不到则返回 nullopt（视为最终自然语言回复）。
std::optional<std::string> extract_run_command(const std::string& assistant_text);

// 执行 shell 命令，合并 stdout/stderr；失败时返回可读错误串（不抛）。
// 注意：仅用于本机受控练习，命令来自模型输出，勿接不可信远端。
std::string run_shell(const std::string& command);

}  // namespace swe_agent::agent
