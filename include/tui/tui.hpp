#pragma once

#include <string>

namespace swe_agent::agent {
class SessionManager;
}

namespace swe_agent::tui {

int run(
    agent::SessionManager& session_manager,
    const std::string& model_name);

}  // namespace swe_agent::tui
