#pragma once

#include <string>

namespace swe_agent::tui {

class SlashCommand;

/** @brief 斜杠行解析结果。 */
enum class SlashParseStatus {
    /** 非斜杠输入（空行或不以 '/' 开头）。 */
    NotACommand,
    /** 已匹配命令且参数校验通过。 */
    Ok,
    /** 已匹配命令但参数非法。 */
    UsageError,
    /** 以 '/' 开头但未注册的命令。 */
    Unknown,
};

/** @brief parse() 的结构化输出。 */
struct SlashParseResult {
    SlashParseStatus status{SlashParseStatus::NotACommand};
    /** 不含前导 '/' 的命令名（在 Ok / UsageError / Unknown 时有意义）。 */
    std::string name;
    /** 命令名之后的剩余参数（已 trim）。 */
    std::string args;
    /** UsageError / Unknown 时的错误文案。 */
    std::string error;
    /** 仅 Ok 时非空；指向 registry 内对象，不拥有所有权。 */
    const SlashCommand* command{nullptr};
};

/** @brief dispatch() 结果：是否已作为斜杠路径消费。 */
enum class SlashDispatchStatus {
    NotACommand,
    Handled,
};

}  // namespace swe_agent::tui
