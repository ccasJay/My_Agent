#pragma once

#include "config/agent_loader.hpp"

#include <string>

namespace swe_agent::model {
struct ModelClient;
}

namespace swe_agent::tui {

int run(
    model::ModelClient& client,
    const config::AgentConfig& agent_cfg,
    const std::string& model_name);

}  // namespace swe_agent::tui
