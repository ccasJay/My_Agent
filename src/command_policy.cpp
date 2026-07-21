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
    "rm"
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
            .reason = "无法识别要执行的程序，需要人工审核。",
        };
    }
    if (kDeniedPrograms.contains(program)) {
        return {
            .action = PolicyAction::Deny,
            .rule_id = "denied-program",
            .reason = "该命令直接调用了被禁止的高风险程序。",
        };
    }
    if (has_complex_shell_syntax(command)) {
        return {
            .action = PolicyAction::RequireReview,
            .rule_id = "complex-shell-syntax",
            .reason = "命令包含复杂 Shell 语法，无法安全自动审核。",
        };
    }
    if (kShellWrappers.contains(program)) {
        return {
            .action = PolicyAction::RequireReview,
            .rule_id = "shell-wrapper",
            .reason = "命令通过 Shell 包装器间接执行，需要人工审核。",
        };
    }
    return {
        .action = PolicyAction::Allow,
        .rule_id = "default-allow",
        .reason = {},
    };

}
} // namespace swe_agent::agent
