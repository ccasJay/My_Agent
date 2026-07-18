#include "agent/agent_event.hpp"
#include "agent/agent_loop.hpp"
#include "app_cli/cli.hpp"
#include "config/agent_loader.hpp"
#include "config/dotenv_loader.hpp"
#include "model/model.hpp"
#include "model/model_client.hpp"
#include "tui/tui.hpp"

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

void print_agent_event(const swe_agent::agent::AgentEvent& event) {
    using swe_agent::agent::AgentEventType;

    switch (event.type) {
    case AgentEventType::Assistant:
        std::cout << "================= step " << event.step
                  << " (assistant) =================== \n"
                  << event.content << '\n';
        break;
    case AgentEventType::FormatError:
        std::cout << "================= step " << event.step
                  << " (format error, continue) =================== \n"
                  << event.content << '\n';
        break;
    case AgentEventType::CommandStarted:
        break;
    case AgentEventType::CommandFinished:
        if (event.command == "echo COMPLETE_TASK") {
            std::cout << "================= task complete =================== \n";
        } else {
            std::cout << "================= step " << event.step
                      << " (observation) =================== \n";
        }
        std::cout << event.content << '\n';
        break;
    case AgentEventType::Completed:
        std::cout << "================= final =================== \n"
                  << event.content;
        if (event.content.empty() || event.content.back() != '\n') {
            std::cout << '\n';
        }
        break;
    case AgentEventType::Stopped:
        std::cout << "================= stopped ===================\n";
        break;
    case AgentEventType::StepLimitReached:
        std::cout << "================= step limit reached ===================\n";
        break;
    case AgentEventType::EmptyResponse:
        std::cout << "================= empty response ===================\n";
        break;
    }
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

        if (!options.model.empty()) {
            env_config.model_name = options.model;
        }

        // 3) Provider 实现
        swe_agent::model::ModelClient client(env_config);

        if (options.use_tui()) {
            return swe_agent::tui::run(client, agent_cfg, env_config.model_name);
        }

        agent_cfg.user_prompt = options.task;
        swe_agent::agent::AgentRunOptions run_options;
        run_options.on_event = print_agent_event;
        (void)swe_agent::agent::run(client, agent_cfg, run_options);
        return 0;
    } catch (const CLI::ParseError& e) {
        return cli.exit(e);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
