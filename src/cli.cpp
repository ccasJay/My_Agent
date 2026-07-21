#include "app_cli/cli.hpp"

namespace swe_agent::cli {

Cli::Cli() {
    task_option_ =
        app_.add_option("-t", options_.task, "Task for non-interactive mode");
    app_.add_option("-m", options_.model, "Model name");
    app_.add_flag(
        "-c,--continue",
        options_.continue_session,
        "Continue the latest session in the current workspace");
}

Cli::RunOption Cli::parse(int argc, char* argv[]) {
    app_.parse(argc, argv);
    options_.task_provided = task_option_->count() > 0;
    return options_;
}

int Cli::exit(const CLI::ParseError& error) const {
    return app_.exit(error);
}

}  // namespace swe_agent::cli
