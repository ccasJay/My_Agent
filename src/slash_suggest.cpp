#include "tui/slash_suggest.hpp"

#include <cctype>

namespace swe_agent::tui {
namespace {

/** ASCII 空白：空格、制表符等（与设计文档 token 边界一致）。 */
bool is_ascii_space(char ch) noexcept {
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

/**
 * 定位光标处的非空白 token（插入点语义，不跨过空白去找左侧词）。
 *
 * - 光标落在非空白字符上：扩展该 token
 * - 光标在串尾或空白上，且紧邻左侧为非空白：取左侧 token（如 "/he|"）
 * - 左侧为空白（如 "/resume |"）：无 token，建议关闭
 */
bool find_token_at_cursor(
    std::string_view input,
    std::size_t cursor,
    std::size_t& begin,
    std::size_t& end) {
    const std::size_t n = input.size();
    if (cursor > n) {
        cursor = n;
    }

    std::size_t probe = 0;
    bool found = false;

    if (cursor < n && !is_ascii_space(input[cursor])) {
        // 插入点落在某个字符上（该字符属于 token）
        probe = cursor;
        found = true;
    } else if (cursor > 0 && !is_ascii_space(input[cursor - 1])) {
        // 紧贴 token 末尾（含串尾）：取左侧字符所属 token
        probe = cursor - 1;
        found = true;
    }

    if (!found) {
        return false;
    }

    begin = probe;
    while (begin > 0 && !is_ascii_space(input[begin - 1])) {
        --begin;
    }
    end = probe + 1;
    while (end < n && !is_ascii_space(input[end])) {
        ++end;
    }
    return true;
}

/**
 * usage 在命令词后仍有空白 → 视为需要参数，补全时加尾随空格。
 * 例如 "/resume <session-id-prefix>" → true；"/new" → false。
 */
bool usage_needs_trailing_space(std::string_view usage) noexcept {
    std::size_t i = 0;
    if (!usage.empty() && usage.front() == '/') {
        ++i;
    }
    while (i < usage.size() && !is_ascii_space(usage[i])) {
        ++i;
    }
    return i < usage.size() && is_ascii_space(usage[i]);
}

}  // namespace

SlashSuggestResult evaluate_slash_suggest(
    std::string_view input,
    int cursor,
    const std::vector<const SlashCommand*>& commands) {
    SlashSuggestResult result;

    std::size_t pos = 0;
    if (cursor > 0) {
        pos = static_cast<std::size_t>(cursor);
    }
    if (pos > input.size()) {
        pos = input.size();
    }

    std::size_t token_begin = 0;
    std::size_t token_end = 0;
    if (!find_token_at_cursor(input, pos, token_begin, token_end)) {
        return result;
    }

    const std::string_view token =
        input.substr(token_begin, token_end - token_begin);
    // 仅当 token 以 '/' 开头时激活补全。
    if (token.empty() || token.front() != '/') {
        return result;
    }

    const std::string_view prefix = token.substr(1);
    result.prefix = std::string{prefix};
    result.token_begin = token_begin;
    result.token_end = token_end;

    // 按注册顺序做大小写敏感前缀匹配。
    result.matches.reserve(commands.size());
    for (const SlashCommand* command : commands) {
        if (command == nullptr) {
            continue;
        }
        const std::string_view name = command->name();
        if (!name.starts_with(prefix)) {
            continue;
        }
        result.matches.push_back(SlashSuggestItem{
            .name = std::string{name},
            .usage = std::string{command->usage()},
            .summary = std::string{command->summary()},
        });
    }

    result.open = !result.matches.empty();
    return result;
}

std::string apply_slash_completion(
    std::string_view input,
    const SlashSuggestResult& result,
    std::size_t selected) {
    if (selected >= result.matches.size()) {
        return std::string{input};
    }
    if (result.token_begin > result.token_end ||
        result.token_end > input.size() ||
        result.token_begin > input.size()) {
        return std::string{input};
    }

    const SlashSuggestItem& item = result.matches[selected];
    std::string insertion = "/";
    insertion += item.name;
    if (usage_needs_trailing_space(item.usage)) {
        insertion.push_back(' ');
    }

    std::string out;
    out.reserve(
        input.size() - (result.token_end - result.token_begin) + insertion.size());
    out.append(input.substr(0, result.token_begin));
    out.append(insertion);
    out.append(input.substr(result.token_end));
    return out;
}

int completion_cursor_after(std::string_view completed_input) noexcept {
    // v1：补全后光标固定在缓冲末尾。
    return static_cast<int>(completed_input.size());
}

}  // namespace swe_agent::tui
