#pragma once

#include <string>
#include <string_view>

namespace swe_agent::tui {

/**
 * @brief TUI 斜杠会话命令种类
 */
enum class SessionCommandKind {
    None,
    New,
    List,
    Resume,
    Invalid,
};

/**
 * @brief 解析后的会话命令
 *
 * kind=None 表示非会话命令；Invalid 时 error 含原因；
 * Resume 时 argument 为 id 前缀。
 */
struct SessionCommand {
    SessionCommandKind kind{SessionCommandKind::None};
    std::string argument;
    std::string error;
};

/**
 * @brief 解析用户输入中的 /new /list /resume 等会话命令
 * @param input 整行输入
 * @return 解析结果；非匹配命令返回 kind=None
 */
[[nodiscard]] SessionCommand parse_session_command(std::string_view input);

}  // namespace swe_agent::tui
