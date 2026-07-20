#include "agent/command_policy.hpp"
#include <filesystem>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace swe_agent::agent {
namespace {

const std::unordered_set<std::string_view> kDeniedPrograms {
    "reboot",
    "shutdown",
    "rm"
};

std::string extract_program(std::string_view command) {
    std::istringstream input{std::string{command}};
    std::string first_token;

    if(!(input>>first_token)) {
        return {};
    }

    return std::filesystem::path{first_token}.filename().string();
}
} // namespace

PolicyResult evaluate_command_policy(std::string_view command, const PolicyContext &context) {
    (void)context;
    const std::string program =extract_program(command);

    if (program.empty()) {
        return {
            .action = PolicyAction::RequireReview,
            .rule_id = "empty-command",
            .reason = "Unable to identify executable program",
        };
    }
    if (kDeniedPrograms.contains(program)) {
        return {
            .action = PolicyAction::Deny,
            .rule_id = " denied-program ",
            .reason = "This is a dangerous operation !",
        };
    }
    return {
        .action = PolicyAction::Allow,
        .rule_id = "default-allow",
        .reason = {},
    };

}
} // namespace swe_agent::agent
