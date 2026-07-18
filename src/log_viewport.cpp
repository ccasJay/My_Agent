#include "tui/log_viewport.hpp"

#include <algorithm>

namespace swe_agent::tui {

bool LogViewport::sync(std::size_t line_count) noexcept {
    const std::size_t previous_line = current_line_;
    line_count_ = line_count;

    if (line_count_ == 0) {
        current_line_ = 0;
        target_line_ = 0;
        following_tail_ = true;
        return previous_line != current_line_;
    }

    const std::size_t last_line = line_count_ - 1;
    if (following_tail_) {
        current_line_ = last_line;
        target_line_ = last_line;
    } else {
        current_line_ = std::min(current_line_, last_line);
        target_line_ = std::min(target_line_, last_line);
    }
    return previous_line != current_line_;
}

void LogViewport::follow_tail() noexcept {
    following_tail_ = true;
    if (line_count_ > 0) {
        current_line_ = line_count_ - 1;
        target_line_ = current_line_;
    }
}

bool LogViewport::scroll_up(std::size_t lines) noexcept {
    if (line_count_ == 0) {
        return false;
    }

    following_tail_ = false;
    target_line_ = target_line_ > lines ? target_line_ - lines : 0;
    return animation_pending();
}

bool LogViewport::scroll_down(std::size_t lines) noexcept {
    if (line_count_ == 0) {
        return false;
    }

    following_tail_ = false;
    const std::size_t last_line = line_count_ - 1;
    // 使用剩余距离做饱和加法，避免 target_line_ + lines 溢出。
    const std::size_t remaining = last_line - target_line_;
    target_line_ += std::min(lines, remaining);
    if (!animation_pending() && target_line_ == last_line) {
        following_tail_ = true;
    }
    return animation_pending();
}

bool LogViewport::tick() noexcept {
    const std::size_t previous_line = current_line_;
    if (current_line_ < target_line_) {
        const std::size_t distance = target_line_ - current_line_;
        // 限制单帧跨度，避免长距离滚动首帧直接跳过大半日志。
        const std::size_t step = std::clamp<std::size_t>(distance / 4, 1, 16);
        current_line_ += step;
    } else if (current_line_ > target_line_) {
        const std::size_t distance = current_line_ - target_line_;
        const std::size_t step = std::clamp<std::size_t>(distance / 4, 1, 16);
        current_line_ -= step;
    }

    if (!animation_pending() && line_count_ > 0 &&
        current_line_ == line_count_ - 1) {
        following_tail_ = true;
    }
    return previous_line != current_line_;
}

bool LogViewport::home() noexcept {
    const bool changed = current_line_ != 0;
    following_tail_ = false;
    current_line_ = 0;
    target_line_ = 0;
    return changed;
}

bool LogViewport::end() noexcept {
    const bool changed =
        line_count_ > 0 && current_line_ != line_count_ - 1;
    follow_tail();
    return changed;
}

bool LogViewport::jump_to(std::size_t line) noexcept {
    if (line_count_ == 0) {
        return false;
    }

    const std::size_t next_line = std::min(line, line_count_ - 1);
    const bool changed = current_line_ != next_line;
    current_line_ = next_line;
    target_line_ = next_line;
    following_tail_ = false;
    return changed;
}

std::size_t LogViewport::current_line() const noexcept {
    return current_line_;
}

bool LogViewport::following_tail() const noexcept {
    return following_tail_;
}

bool LogViewport::animation_pending() const noexcept {
    return current_line_ != target_line_;
}

LogWindow LogViewport::render_window(std::size_t max_lines) const noexcept {
    if (line_count_ == 0 || max_lines == 0) {
        return {};
    }
    if (line_count_ <= max_lines) {
        return {.begin = 0, .end = line_count_};
    }

    const std::size_t half_window = max_lines / 2;
    std::size_t begin = current_line_ > half_window
        ? current_line_ - half_window
        : 0;
    begin = std::min(begin, line_count_ - max_lines);
    return {.begin = begin, .end = begin + max_lines};
}

}  // namespace swe_agent::tui
