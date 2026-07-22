#include "app_cli/acp_cli.hpp"

namespace swe_agent::cli {

AcpCli::AcpCli() {
    app_.add_option(
        "--env-file",
        options_.env_file,
        "Path to the dotenv configuration file");
    app_.add_option(
        "--agent-config",
        options_.agent_config,
        "Path to the agent YAML configuration file");
    app_.add_option("-m,--model", options_.model, "Model name override");
}

AcpRunOptions AcpCli::parse(int argc, char* argv[]) {
    app_.parse(argc, argv);
    return options_;
}

int AcpCli::exit(const CLI::ParseError& error) const {
    return app_.exit(error);
}

}  // namespace swe_agent::cli
