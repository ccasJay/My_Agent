#pragma once

#include <CLI/CLI.hpp>
#include <string>

namespace swe_agent::cli {

class Cli {
public:
    struct RunOption {
        std::string task;
        std::string model;
    };

    Cli();

    RunOption parse(int argc, char* argv[]);
    int exit(const CLI::ParseError& error) const;

private:
    CLI::App app_{"Run SWE Agent"};
    RunOption options_;
};

}  // namespace swe_agent::cli
