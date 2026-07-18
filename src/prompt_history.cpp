#include "tui/prompt_history.hpp"

#include <utility>

namespace swe_agent::tui {

void PromptHistory::record(std::string prompt) {
    if (!prompt.empty() && (entries_.empty() || entries_.back() != prompt)) {
        entries_.push_back(std::move(prompt));
    }
    cancel_navigation();
}

bool PromptHistory::previous(std::string& input) {
    if (entries_.empty()) {
        return false;
    }

    if (!index_) {
        draft_ = input;
        index_ = entries_.size() - 1;
    } else if (*index_ > 0) {
        --*index_;
    }
    input = entries_[*index_];
    return true;
}

bool PromptHistory::next(std::string& input) {
    if (!index_) {
        return false;
    }

    if (*index_ + 1 < entries_.size()) {
        ++*index_;
        input = entries_[*index_];
    } else {
        index_.reset();
        input = std::move(draft_);
        draft_.clear();
    }
    return true;
}

void PromptHistory::cancel_navigation() {
    index_.reset();
    draft_.clear();
}

std::size_t PromptHistory::size() const noexcept {
    return entries_.size();
}

}  // namespace swe_agent::tui
