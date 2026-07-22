#pragma once

#include <cctype>
#include <sstream>
#include <string>
#include <string_view>

namespace swe_agent::agent {

/** @brief 判断一行是否为允许前置空白的 RUN 指令。 */
inline bool is_run_line(std::string_view line) {
    std::size_t first = 0;
    while (first < line.size() &&
           (line[first] == ' ' || line[first] == '\t')) {
        ++first;
    }
    return line.substr(first).starts_with("RUN:");
}

/** @brief 去除 Assistant 文本中的所有 RUN 指令行。 */
inline std::string strip_run_lines(std::string_view text) {
    std::istringstream input{std::string{text}};
    std::string output;
    std::string line;
    while (std::getline(input, line)) {
        if (is_run_line(line)) {
            continue;
        }
        if (!output.empty()) {
            output.push_back('\n');
        }
        output += line;
    }
    return output;
}

/** @brief 判断文本是否至少包含一个非空白字符。 */
inline bool has_visible_text(std::string_view text) {
    for (unsigned char character : text) {
        if (!std::isspace(character)) {
            return true;
        }
    }
    return false;
}

}  // namespace swe_agent::agent
