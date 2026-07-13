#include "agent/agent_loop.hpp"
#include "config/agent_loader.hpp"
#include "config/dotenv_loader.hpp"
#include "model/model_client.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

std::string find_env_path() {
    for (const char* path : {".env", "../.env"}) {
        if (std::ifstream{path}.good()) {
            return path;
        }
    }
    throw std::runtime_error{"Cannot find .env (tried .env and ../.env)"};
}

std::string find_agent_path() {
    for (const char* path : {"config/agent.yaml", "../config/agent.yaml"}) {
        if (std::ifstream{path}.good()) {
            return path;
        }
    }
    throw std::runtime_error{"Cannot find agent.yaml"};
}

}  // namespace

int main() {
    try {
        // 1) 密钥 / endpoint
        auto env = swe_agent::config::load_env(find_env_path());
        // 2) 文案配置（system / user）
        auto agent_cfg = swe_agent::config::load_agent(find_agent_path());

        swe_agent::model::ModelConfig env_config{
            .base_url = swe_agent::config::get_required(env, "OPENAI_BASE_URL"),
            .api_key = swe_agent::config::get_required(env, "OPENAI_API_KEY"),
            .model_name = swe_agent::config::get_required(env, "OPENAI_MODEL"),
        };

        // 3) Provider 实现
        swe_agent::model::OpenaiCompatible provider(env_config);

        // 4) 交给 agent loop（内部组 history、多轮 query；会打印最后一轮）
        (void)swe_agent::agent::run(provider, agent_cfg);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
