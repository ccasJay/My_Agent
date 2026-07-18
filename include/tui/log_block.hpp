#pragma once

#include "tui/tui_state.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace swe_agent::tui {

// 面向界面的日志块。CommandStarted 与对应的 CommandFinished 会合并到同一块。
struct TuiLogBlock {
    TuiLogKind kind{TuiLogKind::System};
    std::string heading;
    std::string summary;
    std::string detail;
    bool foldable{false};
    bool expanded{true};
    bool running{false};
};

class TuiLogBlocks {
public:
    // 增量消费日志；返回首个变化的块索引，供渲染层局部重建。
    std::optional<std::size_t> append(
        const std::vector<TuiLogEntry>& entries);
    bool toggle(std::size_t index);

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const std::vector<TuiLogBlock>& blocks() const noexcept;

private:
    std::size_t append_entry(const TuiLogEntry& entry);

    std::vector<TuiLogBlock> blocks_;
    std::optional<std::size_t> running_command_;
};

}  // namespace swe_agent::tui
