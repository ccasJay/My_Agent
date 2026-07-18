#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>

namespace swe_agent::tui {

// Grok 风格的单列 Braille spinner；每帧宽度固定，动画不会推动布局。
[[nodiscard]] std::string_view run_spinner_frame(
    std::size_t frame) noexcept;

// 运行态计时格式：<10s 保留一位小数，之后使用 10s / 1m20s / 1h2m。
[[nodiscard]] std::string format_run_duration(
    std::chrono::steady_clock::duration duration);

// 为状态行补充统一的 Unicode 省略号。
[[nodiscard]] std::string format_run_activity(std::string_view activity);

// 只保存 UI 计时状态：任务切换时重置总计时，活动切换时重置阶段计时。
class RunStatusAnimation {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    void sync(
        bool running,
        std::size_t task_id,
        std::string_view activity,
        TimePoint turn_started_at,
        TimePoint phase_started_at);

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::size_t task_id() const noexcept;
    [[nodiscard]] const std::string& activity() const noexcept;
    [[nodiscard]] Clock::duration turn_elapsed(TimePoint now) const noexcept;
    [[nodiscard]] Clock::duration phase_elapsed(TimePoint now) const noexcept;

private:
    [[nodiscard]] static Clock::duration elapsed(
        TimePoint start,
        TimePoint now) noexcept;

    bool active_{false};
    std::size_t task_id_{0};
    std::string activity_;
    TimePoint turn_started_at_{};
    TimePoint phase_started_at_{};
};

}  // namespace swe_agent::tui
