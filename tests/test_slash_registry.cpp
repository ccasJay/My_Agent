#include <catch2/catch_test_macros.hpp>

#include "tui/slash_command.hpp"
#include "tui/slash_registry.hpp"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using swe_agent::tui::SlashCommand;
using swe_agent::tui::SlashContext;
using swe_agent::tui::SlashParseStatus;
using swe_agent::tui::SlashRegistry;
using swe_agent::tui::register_builtin_slash_commands;

namespace {

class StubCommand final : public SlashCommand {
public:
    explicit StubCommand(
        std::string name,
        std::string usage = {},
        std::string summary = "stub")
        : name_(std::move(name)),
          usage_(usage.empty() ? ("/" + name_) : std::move(usage)),
          summary_(std::move(summary)) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return name_;
    }
    [[nodiscard]] std::string_view summary() const noexcept override {
        return summary_;
    }
    [[nodiscard]] std::string_view usage() const noexcept override {
        return usage_;
    }
    [[nodiscard]] std::optional<std::string> validate(
        std::string_view args) const override {
        if (args.empty()) {
            return std::nullopt;
        }
        return std::string{"Usage: "} + usage_;
    }
    void execute(SlashContext& /*ctx*/, std::string_view /*args*/) const override {
        ++execute_count;
    }

    mutable int execute_count{0};

private:
    std::string name_;
    std::string usage_;
    std::string summary_;
};

}  // namespace

TEST_CASE("slash registry parse leaves normal tasks alone", "[tui][slash]") {
    SlashRegistry registry;
    register_builtin_slash_commands(registry);

    const auto result = registry.parse(" fix tests");
    REQUIRE(result.status == SlashParseStatus::NotACommand);
}

TEST_CASE(
    "slash registry parse recognizes builtin management commands",
    "[tui][slash]") {
    SlashRegistry registry;
    register_builtin_slash_commands(registry);

    REQUIRE(registry.parse(" /new ").status == SlashParseStatus::Ok);
    REQUIRE(registry.parse(" /new ").name == "new");
    REQUIRE(registry.parse("/sessions").status == SlashParseStatus::Ok);
    REQUIRE(registry.parse("/sessions").name == "sessions");

    const auto resume = registry.parse("/resume abcdef12");
    REQUIRE(resume.status == SlashParseStatus::Ok);
    REQUIRE(resume.name == "resume");
    REQUIRE(resume.args == "abcdef12");
    REQUIRE(resume.command != nullptr);
}

TEST_CASE(
    "slash registry parse reports malformed resume and unknown commands",
    "[tui][slash]") {
    SlashRegistry registry;
    register_builtin_slash_commands(registry);

    const auto missing_id = registry.parse("/resume");
    REQUIRE(missing_id.status == SlashParseStatus::UsageError);
    REQUIRE(missing_id.error == "Usage: /resume <session-id-prefix>");

    const auto invalid_id = registry.parse("/resume abc def");
    REQUIRE(invalid_id.status == SlashParseStatus::UsageError);
    REQUIRE(
        invalid_id.error == "Session id prefix cannot contain whitespace");

    const auto unknown = registry.parse("/unknown");
    REQUIRE(unknown.status == SlashParseStatus::Unknown);
    REQUIRE(unknown.error == "Unknown command: /unknown");

    const auto glued = registry.parse("/resumeabcdef12");
    REQUIRE(glued.status == SlashParseStatus::Unknown);
    REQUIRE(glued.error == "Unknown command: /resumeabcdef12");
}

TEST_CASE("slash registry rejects duplicate names", "[tui][slash]") {
    SlashRegistry registry;
    registry.register_command(std::make_unique<StubCommand>("demo"));
    REQUIRE_THROWS_AS(
        registry.register_command(std::make_unique<StubCommand>("demo")),
        std::invalid_argument);
}

TEST_CASE("slash registry find and list preserve registration order",
          "[tui][slash]") {
    SlashRegistry registry;
    registry.register_command(std::make_unique<StubCommand>("b"));
    registry.register_command(std::make_unique<StubCommand>("a"));

    REQUIRE(registry.find("a") != nullptr);
    REQUIRE(registry.find("missing") == nullptr);

    const auto listed = registry.list();
    REQUIRE(listed.size() == 2);
    REQUIRE(listed[0]->name() == "b");
    REQUIRE(listed[1]->name() == "a");
}

TEST_CASE("slash help is registered among builtins", "[tui][slash]") {
    SlashRegistry registry;
    register_builtin_slash_commands(registry);

    const auto help = registry.parse("/help");
    REQUIRE(help.status == SlashParseStatus::Ok);
    REQUIRE(help.name == "help");

    const auto listed = registry.list();
    REQUIRE(listed.size() == 4);
    std::vector<std::string> names;
    for (const SlashCommand* command : listed) {
        names.emplace_back(command->name());
    }
    REQUIRE(names == std::vector<std::string>{"new", "sessions", "resume", "help"});
}

TEST_CASE("slash extra args on zero-arg builtins are usage errors",
          "[tui][slash]") {
    SlashRegistry registry;
    register_builtin_slash_commands(registry);

    const auto extra = registry.parse("/new now");
    REQUIRE(extra.status == SlashParseStatus::UsageError);
    REQUIRE(extra.error == "Usage: /new");
}
