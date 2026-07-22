#include "tui/slash_registry.hpp"

#include "agent/session_manager.hpp"
#include "tui/slash_context.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace swe_agent::tui {
namespace {

/** 无参命令：非空 args 时返回 Usage: <usage>。 */
std::optional<std::string> require_no_args(
    std::string_view args,
    std::string_view usage) {
    if (args.empty()) {
        return std::nullopt;
    }
    return std::string{"Usage: "} + std::string{usage};
}

class NewCommand final : public SlashCommand {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "new";
    }
    [[nodiscard]] std::string_view summary() const noexcept override {
        return "Create and switch to a new session";
    }
    [[nodiscard]] std::string_view usage() const noexcept override {
        return "/new";
    }
    [[nodiscard]] std::optional<std::string> validate(
        std::string_view args) const override {
        return require_no_args(args, usage());
    }
    void execute(SlashContext& ctx, std::string_view /*args*/) const override {
        (void)ctx.sessions.new_session();
        (void)ctx.reload_active_session();
    }
};

class SessionsCommand final : public SlashCommand {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "sessions";
    }
    [[nodiscard]] std::string_view summary() const noexcept override {
        return "List sessions in the current workspace";
    }
    [[nodiscard]] std::string_view usage() const noexcept override {
        return "/sessions";
    }
    [[nodiscard]] std::optional<std::string> validate(
        std::string_view args) const override {
        return require_no_args(args, usage());
    }
    void execute(SlashContext& ctx, std::string_view /*args*/) const override {
        std::ostringstream content;
        const auto sessions = ctx.sessions.list_sessions();
        for (const auto& summary : sessions) {
            const std::string title =
                summary.title.empty() ? "(untitled)" : summary.title;
            content << summary.id.substr(
                           0, std::min<std::size_t>(8, summary.id.size()))
                    << "  " << title << "  [" << summary.model_name << "]\n";
        }
        ctx.notice(
            "Sessions",
            content.str().empty() ? "No sessions in this workspace."
                                  : content.str());
    }
};

class ResumeCommand final : public SlashCommand {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "resume";
    }
    [[nodiscard]] std::string_view summary() const noexcept override {
        return "Resume a session by id prefix";
    }
    [[nodiscard]] std::string_view usage() const noexcept override {
        return "/resume <session-id-prefix>";
    }
    [[nodiscard]] std::optional<std::string> validate(
        std::string_view args) const override {
        if (args.empty()) {
            return std::string{"Usage: /resume <session-id-prefix>"};
        }
        if (args.find_first_of(" \t\r\n") != std::string_view::npos) {
            return std::string{"Session id prefix cannot contain whitespace"};
        }
        return std::nullopt;
    }
    void execute(SlashContext& ctx, std::string_view args) const override {
        (void)ctx.sessions.resume(args);
        (void)ctx.reload_active_session();
    }
};

class HelpCommand final : public SlashCommand {
public:
    explicit HelpCommand(const SlashRegistry& registry) : registry_(registry) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return "help";
    }
    [[nodiscard]] std::string_view summary() const noexcept override {
        return "Show available slash commands";
    }
    [[nodiscard]] std::string_view usage() const noexcept override {
        return "/help";
    }
    [[nodiscard]] std::optional<std::string> validate(
        std::string_view args) const override {
        return require_no_args(args, usage());
    }
    void execute(SlashContext& ctx, std::string_view /*args*/) const override {
        std::ostringstream content;
        for (const SlashCommand* command : registry_.list()) {
            content << command->usage() << "  —  " << command->summary()
                    << '\n';
        }
        ctx.notice("Help", content.str());
    }

private:
    const SlashRegistry& registry_;
};

}  // namespace

void register_builtin_slash_commands(SlashRegistry& registry) {
    registry.register_command(std::make_unique<NewCommand>());
    registry.register_command(std::make_unique<SessionsCommand>());
    registry.register_command(std::make_unique<ResumeCommand>());
    registry.register_command(std::make_unique<HelpCommand>(registry));
}

}  // namespace swe_agent::tui
