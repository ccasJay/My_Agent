#include "tui/run_status.hpp"

#include <array>
#include <chrono>
#include <string>

namespace swe_agent::tui {

std::string_view run_spinner_frame(std::size_t frame) noexcept {
    static constexpr std::array<std::string_view, 8> kFrames{
        "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧",
    };
    return kFrames[frame % kFrames.size()];
}

std::string format_run_duration(
    std::chrono::steady_clock::duration duration) {
    using namespace std::chrono;

    const auto raw_milliseconds = duration_cast<milliseconds>(duration).count();
    const auto milliseconds_count = raw_milliseconds > 0 ? raw_milliseconds : 0;
    if (milliseconds_count < 10'000) {
        const auto tenths = milliseconds_count / 100;
        return std::to_string(tenths / 10) + "." +
            std::to_string(tenths % 10) + "s";
    }

    const auto seconds_count = milliseconds_count / 1000;
    if (seconds_count < 60) {
        return std::to_string(seconds_count) + "s";
    }
    if (seconds_count < 3600) {
        return std::to_string(seconds_count / 60) + "m" +
            std::to_string(seconds_count % 60) + "s";
    }
    return std::to_string(seconds_count / 3600) + "h" +
        std::to_string((seconds_count % 3600) / 60) + "m";
}

std::string format_run_activity(std::string_view activity) {
    if (activity.empty()) {
        return "Working…";
    }
    if (activity.ends_with("…")) {
        return std::string{activity};
    }
    return std::string{activity} + "…";
}

void RunStatusAnimation::sync(
    bool running,
    std::size_t task_id,
    std::string_view activity,
    TimePoint turn_started_at,
    TimePoint phase_started_at) {
    if (!running) {
        active_ = false;
        activity_.clear();
        return;
    }

    active_ = true;
    task_id_ = task_id;
    activity_ = activity;
    turn_started_at_ = turn_started_at;
    phase_started_at_ = phase_started_at;
}

bool RunStatusAnimation::active() const noexcept {
    return active_;
}

std::size_t RunStatusAnimation::task_id() const noexcept {
    return task_id_;
}

const std::string& RunStatusAnimation::activity() const noexcept {
    return activity_;
}

RunStatusAnimation::Clock::duration RunStatusAnimation::turn_elapsed(
    TimePoint now) const noexcept {
    return active_ ? elapsed(turn_started_at_, now) : Clock::duration::zero();
}

RunStatusAnimation::Clock::duration RunStatusAnimation::phase_elapsed(
    TimePoint now) const noexcept {
    return active_ ? elapsed(phase_started_at_, now) : Clock::duration::zero();
}

RunStatusAnimation::Clock::duration RunStatusAnimation::elapsed(
    TimePoint start,
    TimePoint now) noexcept {
    return now > start ? now - start : Clock::duration::zero();
}

}  // namespace swe_agent::tui
