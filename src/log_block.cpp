#include "tui/log_block.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace swe_agent::tui {
namespace {

std::string without_repeated_summary(
    std::string_view detail,
    std::string_view summary) {
    if (!summary.empty() && detail.starts_with(summary)) {
        detail.remove_prefix(summary.size());
        if (!detail.empty() && detail.front() == '\n') {
            detail.remove_prefix(1);
        }
    }
    return std::string{detail};
}

bool closes_running_command(TuiLogKind kind) {
    return kind == TuiLogKind::Task || kind == TuiLogKind::Command ||
        kind == TuiLogKind::Final || kind == TuiLogKind::Error;
}

}  // namespace

std::optional<std::size_t> TuiLogBlocks::append(
    const std::vector<TuiLogEntry>& entries) {
    std::optional<std::size_t> first_changed;
    for (const auto& entry : entries) {
        const std::size_t changed = append_entry(entry);
        first_changed = first_changed
            ? std::min(*first_changed, changed)
            : changed;
    }
    return first_changed;
}

bool TuiLogBlocks::toggle(std::size_t index) {
    if (index >= blocks_.size() || !blocks_[index].foldable) {
        return false;
    }
    blocks_[index].expanded = !blocks_[index].expanded;
    return true;
}

std::size_t TuiLogBlocks::size() const noexcept {
    return blocks_.size();
}

bool TuiLogBlocks::empty() const noexcept {
    return blocks_.empty();
}

const std::vector<TuiLogBlock>& TuiLogBlocks::blocks() const noexcept {
    return blocks_;
}

std::size_t TuiLogBlocks::append_entry(const TuiLogEntry& entry) {
    if (entry.kind == TuiLogKind::Observation && running_command_) {
        const std::size_t changed = *running_command_;
        TuiLogBlock& command = blocks_[*running_command_];
        command.detail = without_repeated_summary(entry.content, command.summary);
        command.running = false;
        command.expanded = false;
        running_command_.reset();
        return changed;
    }

    std::optional<std::size_t> first_changed;
    if (running_command_ && closes_running_command(entry.kind)) {
        first_changed = *running_command_;
        TuiLogBlock& command = blocks_[*running_command_];
        command.running = false;
        command.expanded = false;
        running_command_.reset();
    }

    TuiLogBlock block{
        .kind = entry.kind,
        .heading = entry.heading,
        .detail = entry.content,
    };
    if (entry.kind == TuiLogKind::Command) {
        block.summary = entry.content;
        block.detail.clear();
        block.foldable = true;
        block.running = true;
        running_command_ = blocks_.size();
    }
    const std::size_t changed = blocks_.size();
    blocks_.push_back(std::move(block));
    return first_changed ? std::min(*first_changed, changed) : changed;
}

}  // namespace swe_agent::tui
