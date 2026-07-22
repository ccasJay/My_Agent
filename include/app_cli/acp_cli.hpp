#pragma once

#include <CLI/CLI.hpp>

#include <string>

namespace swe_agent::cli {

/** @brief ACP 子进程入口的命令行参数。 */
struct AcpRunOptions {
    /** @brief 显式 dotenv 文件路径；为空时使用默认发现规则。 */
    std::string env_file;
    /** @brief 显式 Agent YAML 路径；为空时使用默认发现规则。 */
    std::string agent_config;
    /** @brief 本次进程的可选模型覆盖。 */
    std::string model;
};

/** @brief 解析 agent-acp 启动参数。 */
class AcpCli {
public:
    /** @brief 构造 ACP 命令行解析器并注册选项。 */
    AcpCli();

    /** @brief 解析命令行并返回启动参数。 */
    AcpRunOptions parse(int argc, char* argv[]);

    /** @brief 输出 CLI11 解析结果并返回对应退出码。 */
    int exit(const CLI::ParseError& error) const;

private:
    CLI::App app_{"Run My Agent as an ACP subprocess"};
    AcpRunOptions options_;
};

}  // namespace swe_agent::cli
