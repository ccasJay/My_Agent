#pragma once

#include <filesystem>
#include <string>
#include <string_view>
namespace swe_agent::agent {

enum class PolicyAction {
    Allow,
    RequireReview,
    Deny,
};

struct PolicyContext {
    std::filesystem::path working_dir;
    std::filesystem::path workspace_root;
};

struct PolicyResult {
    PolicyAction action;
    std::string rule_id; 
    std::string reason;
};

PolicyResult evaluate_command_policy(
    std::string_view command,
    const PolicyContext& context
);

}