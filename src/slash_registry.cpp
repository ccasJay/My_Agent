#include "tui/slash_registry.hpp"

#include "tui/slash_context.hpp"

#include <cctype>
#include <stdexcept>
#include <utility>

namespace swe_agent::tui {
namespace {

std::string_view trim(std::string_view value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

}  // namespace

void SlashRegistry::register_command(std::unique_ptr<SlashCommand> command) {
    if (command == nullptr) {
        throw std::invalid_argument("Slash command must not be null");
    }
    const std::string name{command->name()};
    if (name.empty()) {
        throw std::invalid_argument("Slash command name must not be empty");
    }
    if (by_name_.contains(name)) {
        throw std::invalid_argument("Duplicate slash command: " + name);
    }
    for (const std::string_view alias : command->aliases()) {
        const std::string alias_name{alias};
        if (alias_name.empty() || by_name_.contains(alias_name)) {
            throw std::invalid_argument(
                "Duplicate or empty slash command alias: " + alias_name);
        }
    }

    SlashCommand* raw = command.get();
    commands_.push_back(std::move(command));
    by_name_.emplace(name, raw);
    for (const std::string_view alias : raw->aliases()) {
        by_name_.emplace(std::string{alias}, raw);
    }
}

const SlashCommand* SlashRegistry::find(std::string_view name) const noexcept {
    const auto it = by_name_.find(std::string{name});
    if (it == by_name_.end()) {
        return nullptr;
    }
    return it->second;
}

std::vector<const SlashCommand*> SlashRegistry::list() const {
    std::vector<const SlashCommand*> result;
    result.reserve(commands_.size());
    for (const auto& command : commands_) {
        result.push_back(command.get());
    }
    return result;
}

SlashParseResult SlashRegistry::parse(std::string_view line) const {
    line = trim(line);
    if (line.empty() || line.front() != '/') {
        return {};
    }

    // 命令 token：'/' 后连续非空白（/resumeabcdef12 视为未知整词）。
    std::size_t token_end = 1;
    while (token_end < line.size() &&
           std::isspace(static_cast<unsigned char>(line[token_end])) == 0) {
        ++token_end;
    }
    const std::string_view token = line.substr(0, token_end);
    const std::string name = std::string{token.substr(1)};
    const std::string args{trim(line.substr(token_end))};

    const SlashCommand* command = find(name);
    if (command == nullptr) {
        return {
            .status = SlashParseStatus::Unknown,
            .name = name,
            .args = args,
            .error = "Unknown command: " + std::string{token},
        };
    }

    if (const std::optional<std::string> error = command->validate(args);
        error.has_value()) {
        return {
            .status = SlashParseStatus::UsageError,
            .name = name,
            .args = args,
            .error = *error,
            .command = command,
        };
    }

    return {
        .status = SlashParseStatus::Ok,
        .name = name,
        .args = args,
        .command = command,
    };
}

SlashDispatchStatus SlashRegistry::dispatch(
    SlashContext& ctx,
    std::string_view line) const {
    const SlashParseResult parsed = parse(line);
    switch (parsed.status) {
    case SlashParseStatus::NotACommand:
        return SlashDispatchStatus::NotACommand;
    case SlashParseStatus::Unknown:
    case SlashParseStatus::UsageError:
        ctx.notice_error(parsed.error);
        return SlashDispatchStatus::Handled;
    case SlashParseStatus::Ok:
        try {
            parsed.command->execute(ctx, parsed.args);
        } catch (const std::exception& error) {
            ctx.notice("Session error", error.what(), true);
        }
        return SlashDispatchStatus::Handled;
    }
    return SlashDispatchStatus::NotACommand;
}

}  // namespace swe_agent::tui
