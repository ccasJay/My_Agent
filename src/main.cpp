#include "agent/agent_loop.hpp"
#include "app_cli/cli.hpp"
#include "config/agent_loader.hpp"
#include "config/dotenv_loader.hpp"
#include "model/model.hpp"
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

int main(int argc, char* argv[]) {
    swe_agent::cli::Cli cli;

    try {
        // 解析命令行参数
        const auto options = cli.parse(argc, argv);

        // 1) 密钥 / endpoint
        auto env = swe_agent::config::load_env(find_env_path());
        // 2) Prompt 配置（system / user）
        auto agent_cfg = swe_agent::config::load_agent(find_agent_path());

        swe_agent::model::ModelConfig env_config{
            .base_url = swe_agent::config::get_required(env, "OPENAI_BASE_URL"),
            .api_key = swe_agent::config::get_required(env, "OPENAI_API_KEY"),
            .model_name = swe_agent::config::get_required(env, "OPENAI_MODEL"),
        };

        agent_cfg.user_prompt = options.task;
        if (!options.model.empty()) {
            env_config.model_name = options.model;
        }

        // 3) Provider 实现
        swe_agent::model::ModelClient client(env_config);

        // 4) 交给 agent loop（内部组 history、多轮 query；会打印最后一轮）
        (void)swe_agent::agent::run(client, agent_cfg);
        return 0;
    } catch (const CLI::ParseError& e) {
        return cli.exit(e);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
