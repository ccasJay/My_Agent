#pragma once

#include <optional>
#include <string>

namespace swe_agent::config {


struct AgentConfig {
    std::string system;
    std::string user;
    std::optional<std::string> model_name;
};

AgentConfig load_agent(const std::string& path);

}  // namespace swe_agent::config