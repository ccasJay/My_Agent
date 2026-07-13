#pragma once

#include <optional>
#include <string>

namespace swe_agent::config {


struct AgentConfig {
    std::string system_prompt;
    std::string user_prompt;
    std::optional<std::string> model_name;
    std::size_t step_limit = 1; //  yaml 中缺失时默认
};

AgentConfig load_agent(const std::string& path);

}  // namespace swe_agent::config