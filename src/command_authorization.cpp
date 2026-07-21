#include "agent/command_authorization.hpp"

#include <utility>

namespace swe_agent::agent {

CommandDecision authorize_command(
    const CommandRequest& request,
    const PolicyContext& context,
    bool review_all,
    const CommandAuthorizer& reviewer) {
    const PolicyResult policy = evaluate_command_policy(request.command, context);

    if (policy.action == PolicyAction::Deny) {
        return {
            .action = CommandAction::Reject,
            .rule_id = policy.rule_id,
            .reason = policy.reason,
        };
    }

    const bool needs_review =
        policy.action == PolicyAction::RequireReview || review_all;
    if (!needs_review) {
        return {
            .action = CommandAction::Approve,
            .rule_id = policy.rule_id,
            .reason = policy.reason,
        };
    }

    if (!reviewer) {
        return {
            .action = CommandAction::Reject,
            .rule_id = policy.rule_id,
            .reason = "需要人工审核，但当前没有可用的交互审核器。",
        };
    }

    CommandDecision decision = reviewer(request);
    if (decision.action == CommandAction::Reject) {
        if (decision.rule_id.empty()) {
            decision.rule_id = "user_rejected";
        }
        if (decision.reason.empty()) {
            decision.reason = "用户拒绝执行该命令。";
        }
        return decision;
    }

    if (decision.rule_id.empty()) {
        decision.rule_id = policy.rule_id;
    }
    if (decision.reason.empty()) {
        decision.reason = policy.reason;
    }
    return decision;
}

}  // namespace swe_agent::agent
