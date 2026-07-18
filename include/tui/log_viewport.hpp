#pragma once

#include <cstddef>

namespace swe_agent::tui {

struct LogWindow {
    std::size_t begin{0};
    std::size_t end{0};
};

// 与 FTXUI 解耦的日志游标。current_line_ 逐 tick 靠近 target_line_，
// 从而让按页/滚轮跳转表现为平滑滚动。
class LogViewport {
public:
    // 同步日志总行数；返回可见游标是否随之改变。
    bool sync(std::size_t line_count) noexcept;
    // 立即定位到当前末尾，并让后续新增日志继续自动跟随。
    void follow_tail() noexcept;

    // 更新目标位置；返回是否还需要动画 tick。
    bool scroll_up(std::size_t lines) noexcept;
    bool scroll_down(std::size_t lines) noexcept;
    // 按剩余距离向目标缓动；返回当前可见游标是否改变。
    bool tick() noexcept;

    // 立即跳转；返回当前可见游标是否改变。
    bool home() noexcept;
    bool end() noexcept;
    // 立即定位到指定逻辑行，并暂停尾部跟随。
    bool jump_to(std::size_t line) noexcept;

    [[nodiscard]] std::size_t current_line() const noexcept;
    [[nodiscard]] bool following_tail() const noexcept;
    [[nodiscard]] bool animation_pending() const noexcept;
    // 返回以当前行为中心的半开渲染区间 [begin, end)。
    [[nodiscard]] LogWindow render_window(
        std::size_t max_lines) const noexcept;

private:
    std::size_t line_count_{0};
    std::size_t current_line_{0};
    std::size_t target_line_{0};
    bool following_tail_{true};
};

}  // namespace swe_agent::tui
