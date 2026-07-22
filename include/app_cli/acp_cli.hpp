#pragma once

#include <CLI/CLI.hpp>

#include <string>

namespace swe_agent::cli {

/** @brief ACP 子进程入口的命令行参数。 */
struct AcpRunOptions {
    std::string env_file;
    std::string agent_config;
    std::string model;
};

/** @brief 解析 agent-acp 启动参数。 */
class AcpCli {
public:
    AcpCli();

    AcpRunOptions parse(int argc, char* argv[]);
    int exit(const CLI::ParseError& error) const;

private:
    CLI::App app_{"Run My Agent as an ACP subprocess"};
    AcpRunOptions options_;
};

}  // namespace swe_agent::cli
