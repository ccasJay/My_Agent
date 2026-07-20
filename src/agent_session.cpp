#include "agent/agent_session.hpp"
#include "agent/agent_loop.hpp"

#include <utility>

namespace swe_agent::agent {
AgentSession::AgentSession(
    model::ModelClient& client,
    config::AgentConfig config
) : client_(client),
    config_(std::move(config)) {
    clear();
}

void AgentSession::clear() {
    history_.clear();
    history_.push_back({
        .role = model::Role::System,
        .content = config_.system_prompt,
    });
}

AgentRunResult AgentSession::submit(
    std::string user_message,
    const AgentRunOptions& options) {
    history_.push_back({
        .role = model::Role::User,
        .content = std::move(user_message),
    });
    return run(client_, config_, history_, options);
}

const model::MSG& AgentSession::history() const noexcept {
    return history_;
}

}  // namespace swe_agent::agent
