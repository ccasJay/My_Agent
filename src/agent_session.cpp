#include "agent/agent_session.hpp"
#include "agent/agent_event.hpp"
#include "config/agent_loader.hpp"
#include "model/model.hpp"

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

const model::MSG& AgentSession::history() const noexcept {
    return history_;
}

}  // namespace swe_agent::agent
