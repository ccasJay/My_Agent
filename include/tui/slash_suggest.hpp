#pragma once

#include "tui/slash_command.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace swe_agent::tui {

/** @brief 建议列表中的一项（已拷贝元数据，便于 UI 持有）。 */
struct SlashSuggestItem {
    std::string name;
    std::string usage;
    std::string summary;
};

/**
 * @brief 斜杠建议评估结果。
 *
 * open 为 true 时 matches 非空；token_* 为当前 '/' token 的字节区间 [begin, end)。
 */
struct SlashSuggestResult {
    bool open{false};
    std::string prefix;
    std::size_t token_begin{0};
    std::size_t token_end{0};
    std::vector<SlashSuggestItem> matches;
};

/**
 * @brief 根据输入与光标计算斜杠建议。
 * @param input 完整输入缓冲（字节串）
 * @param cursor FTXUI 字节光标，可越界（内部夹紧）
 * @param commands SlashRegistry::list() 结果，保持注册顺序
 */
[[nodiscard]] SlashSuggestResult evaluate_slash_suggest(
    std::string_view input,
    int cursor,
    const std::vector<const SlashCommand*>& commands);

/**
 * @brief 用选中项替换 active token。
 *
 * 有参命令（usage 在命令词后含空白）补全为 "/name "，否则 "/name"。
 * selected 必须 < result.matches.size()。
 */
[[nodiscard]] std::string apply_slash_completion(
    std::string_view input,
    const SlashSuggestResult& result,
    std::size_t selected);

/** @brief 补全后光标位置（v1：始终在缓冲末尾）。 */
[[nodiscard]] int completion_cursor_after(
    std::string_view completed_input) noexcept;

}  // namespace swe_agent::tui
