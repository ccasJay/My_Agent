#include "app_cli/cli.hpp"

namespace swe_agent::cli {

Cli::Cli() {
    app_.add_option("-t", options_.task, "Task for the agent")->required();
    app_.add_option("-m", options_.model, "Model name");
}

Cli::RunOption Cli::parse(int argc, char* argv[]) {
    app_.parse(argc, argv);
    return options_;
}

int Cli::exit(const CLI::ParseError& error) const {
    return app_.exit(error);
}

}  // namespace swe_agent::cli
