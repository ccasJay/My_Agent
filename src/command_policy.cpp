#include "agent/command_policy.hpp"
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>

namespace swe_agent::agent {
namespace {

const std::unordered_set<std::string_view> kDeniedPrograms {
    "reboot",
    "shutdown",
    "rm",
    "rmdir",
};

const std::unordered_set<std::string_view> kShellWrappers {
    "sudo",
    "sh",
    "bash",
    "zsh",
};

std::string extract_program(std::string_view command) {
    std::istringstream input{std::string{command}};
    std::string first_token;

    if(!(input>>first_token)) {
        return {};
    }

    return std::filesystem::path{first_token}.filename().string();
}

bool has_complex_shell_syntax(std::string_view command) {
    return command.find_first_of(";|<>&`\n\r") != std::string_view::npos ||
        command.find("$(") != std::string_view::npos;
}
} // namespace

PolicyResult evaluate_command_policy(std::string_view command, const PolicyContext& context) {
    (void)context;
    const std::string program = extract_program(command);

    if (program.empty()) {
        return {
            .action = PolicyAction::RequireReview,
            .rule_id = "empty-command",
            .reason = "Could not identify the program to run; human review is required.",
        };
    }
    if (kDeniedPrograms.contains(program)) {
        return {
            .action = PolicyAction::Deny,
            .rule_id = "denied-program",
            .reason = "The command directly invokes a denied high-risk program.",
        };
    }
    if (has_complex_shell_syntax(command)) {
        return {
            .action = PolicyAction::RequireReview,
            .rule_id = "complex-shell-syntax",
            .reason =
                "The command contains complex shell syntax and cannot be auto-approved safely.",
        };
    }
    if (kShellWrappers.contains(program)) {
        return {
            .action = PolicyAction::RequireReview,
            .rule_id = "shell-wrapper",
            .reason =
                "The command runs indirectly through a shell wrapper; human review is required.",
        };
    }
    return {
        .action = PolicyAction::Allow,
        .rule_id = "default-allow",
        .reason = {},
    };

}
} // namespace swe_agent::agent
