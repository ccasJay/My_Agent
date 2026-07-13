#include "config/agent_loader.hpp"

#include <stdexcept>
#include <string>
#include <yaml-cpp/yaml.h>

namespace swe_agent::config {

AgentConfig load_agent(const std::string& path) {
    YAML::Node root = YAML::LoadFile(path);

    AgentConfig cfg;

    if (const YAML::Node agent = root["agent"]) {
        if (agent["system"]) {
            cfg.system_prompt = agent["system"].as<std::string>();
        }
        if (agent["user"]) {
            cfg.user_prompt = agent["user"].as<std::string>();
        }
        if (agent["step_limit"]) {
            cfg.step_limit = agent["step_limit"].as<std::size_t>();
        }
    }

    if (const YAML::Node model = root["model"]) {
        if (model["model_name"]) {
            cfg.model_name = model["model_name"].as<std::string>();
        }
    }

    if (cfg.user_prompt.empty()) {
        throw std::runtime_error{"agent.user is required in " + path};
    }

    return cfg;
}

}  // namespace swe_agent::config
