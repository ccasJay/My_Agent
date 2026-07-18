#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace swe_agent::tui {

// 单次 TUI 会话内的输入历史；导航结束时会恢复用户尚未提交的草稿。
class PromptHistory {
public:
    void record(std::string prompt);
    bool previous(std::string& input);
    bool next(std::string& input);
    void cancel_navigation();

    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::vector<std::string> entries_;
    std::optional<std::size_t> index_;
    std::string draft_;
};

}  // namespace swe_agent::tui
