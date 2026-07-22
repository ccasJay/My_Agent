#include "acp/acp_server.hpp"
#include "acp/json_rpc.hpp"
#include "agent/session_paths.hpp"
#include "agent/sqlite_session_store.hpp"
#include "app_cli/acp_cli.hpp"
#include "config/agent_loader.hpp"
#include "config/dotenv_loader.hpp"
#include "model/model_client.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::string discover_file(
    const std::string& explicit_path,
    std::initializer_list<const char*> candidates,
    std::string_view description) {
    if (!explicit_path.empty()) {
        return explicit_path;
    }
    for (const char* candidate : candidates) {
        if (std::ifstream{candidate}.good()) {
            return candidate;
        }
    }
    throw std::runtime_error{
        "Could not find " + std::string{description}};
}

}  // namespace

int main(int argc, char* argv[]) {
    swe_agent::cli::AcpCli cli;
    try {
        const auto options = cli.parse(argc, argv);
        const std::string env_path = discover_file(
            options.env_file,
            {".env", "../.env"},
            ".env");
        const std::string agent_path = discover_file(
            options.agent_config,
            {"config/agent.yaml", "../config/agent.yaml"},
            "agent.yaml");

        const auto env = swe_agent::config::load_env(env_path);
        auto agent_config = swe_agent::config::load_agent(agent_path);
        swe_agent::model::ModelConfig model_config{
            .base_url = swe_agent::config::get_required(env, "OPENAI_BASE_URL"),
            .api_key = swe_agent::config::get_required(env, "OPENAI_API_KEY"),
            .model_name = swe_agent::config::get_required(env, "OPENAI_MODEL"),
        };
        if (!options.model.empty()) {
            model_config.model_name = options.model;
        }

        swe_agent::model::ModelClient provider{model_config};
        swe_agent::agent::SqliteSessionStore session_store{
            swe_agent::agent::session_database_path()};
        swe_agent::acp::JsonRpcConnection connection{std::cin, std::cout};
        swe_agent::acp::AcpServer server{
            connection,
            {
                .provider = provider,
                .agent_config = std::move(agent_config),
                .session_store = session_store,
                .model_name = model_config.model_name,
            },
        };
        return server.run();
    } catch (const CLI::ParseError& error) {
        return cli.exit(error);
    } catch (const std::exception& error) {
        std::cerr << "ACP fatal error: " << error.what() << '\n';
        return 1;
    }
}
