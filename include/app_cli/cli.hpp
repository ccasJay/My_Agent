#pragma once

#include <CLI/CLI.hpp>
#include <string>

namespace swe_agent::cli {

class Cli {
public:
    struct RunOption {
        std::string task;
        std::string model;
        bool continue_session{false};
        // 必须记录参数是否出现，不能用 task.empty() 区分 `-t ""` 与无 -t。
        bool task_provided{false};

        [[nodiscard]] bool use_tui() const noexcept {
            return !task_provided;
        }
    };

    Cli();

    RunOption parse(int argc, char* argv[]);
    int exit(const CLI::ParseError& error) const;

private:
    CLI::App app_{"Run SWE Agent"};
    RunOption options_;
    CLI::Option* task_option_{nullptr};
};

}  // namespace swe_agent::cli
