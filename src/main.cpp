#include "config/dotenv_loader.hpp"
#include "config/agent_loader.hpp"
#include "model/message.hpp"
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
    for (const char* path : {"config/agent.yaml" , "../config/agent.yaml"}) {
        if (std::ifstream{path}.good()) {
            return path;
        }
    }
    throw std::runtime_error{"Cannot find agent.yaml"};
}

}  // namespace

int main() {
    try {
        // 1) .env → map
        auto env = swe_agent::config::load_env(find_env_path());
        auto agent = swe_agent::config::load_agent(find_agent_path());
        // 2) map → ModelConfig（替换 query 里靠 getenv 兜底）
        auto config = swe_agent::model::ModelConfig {
            .model_name = swe_agent::config::get_required(env, "OPENAI_MODEL"),
            .api_key = swe_agent::config::get_required(env, "OPENAI_API_KEY"),
            .base_url = swe_agent::config::get_required(env, "OPENAI_BASE_URL")
        };
        
        // 3) 构造 client（config 已填满，query 不必再 getenv）
        swe_agent::model::OpenaiCompatible client(config);

        // 4) 调模型
        // messages 参数还没真正拼进请求；这里先把调用链跑通。
        swe_agent::model::MSG messages{
            {swe_agent::model::Role::System, agent.system},
            {swe_agent::model::Role::User,agent.user}

        };
        const swe_agent::model::ModelResponse response = client.query(messages);
        std::cout << response.content << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
