#include "agent/agent_event.hpp"
#include "agent/command_authorization.hpp"
#include "agent/command_policy.hpp"
#include "agent/session_manager.hpp"
#include "agent/session_paths.hpp"
#include "agent/sqlite_session_store.hpp"
#include "app_cli/cli.hpp"
#include "app_cli/command_review.hpp"
#include "config/agent_loader.hpp"
#include "config/dotenv_loader.hpp"
#include "model/model.hpp"
#include "model/model_client.hpp"
#include "tui/tui.hpp"

#include <fstream>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace {

std::string find_env_path() {
    for (const char* path : {".env", "../.env"}) {
        if (std::ifstream{path}.good()) {
            return path;
        }
    }
    throw std::runtime_error{"找不到 .env（已尝试 .env 与 ../.env）。"};
}

std::string find_agent_path() {
    for (const char* path : {"config/agent.yaml", "../config/agent.yaml"}) {
        if (std::ifstream{path}.good()) {
            return path;
        }
    }
    throw std::runtime_error{"找不到 agent.yaml。"};
}

void print_agent_event(const swe_agent::agent::AgentEvent& event) {
    using swe_agent::agent::AgentEventType;

    switch (event.type) {
    case AgentEventType::Assistant:
        std::cout << "================= 第 " << event.step
                  << " 步（助手）=================== \n"
                  << event.content << '\n';
        break;
    case AgentEventType::FormatError:
        std::cout << "================= 第 " << event.step
                  << " 步（格式错误，继续执行）=================== \n"
                  << event.content << '\n';
        break;
    case AgentEventType::CommandStarted:
        break;
    case AgentEventType::CommandFinished:
        if (event.command == "echo COMPLETE_TASK") {
            std::cout << "================= 任务完成 =================== \n";
        } else {
            std::cout << "================= 第 " << event.step
                      << " 步（观察结果）=================== \n";
        }
        std::cout << event.content << '\n';
        break;
    case AgentEventType::CommandRejected:
        std::cerr << "命令已拒绝：\n$ " << event.command;
        if (!event.rule_id.empty()) {
            std::cerr << "\n规则：" << event.rule_id;
        }
        if (!event.content.empty()) {
            std::cerr << "\n原因：" << event.content;
        }
        std::cerr << '\n';
        break;
    case AgentEventType::Completed:
        std::cout << "================= 最终结论 =================== \n"
                  << event.content;
        if (event.content.empty() || event.content.back() != '\n') {
            std::cout << '\n';
        }
        break;
    case AgentEventType::Stopped:
        std::cout << "================= 已停止 ===================\n";
        break;
    case AgentEventType::StepLimitReached:
        std::cout << "================= 已达到步骤上限 ===================\n";
        break;
    case AgentEventType::EmptyResponse:
        std::cout << "================= 模型返回为空 ===================\n";
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

        swe_agent::agent::SqliteSessionStore session_store{
            swe_agent::agent::session_database_path()};
        swe_agent::agent::SessionManager session_manager{
            client,
            agent_cfg,
            session_store,
            std::filesystem::canonical(std::filesystem::current_path()).string(),
            env_config.model_name,
        };
        if (options.continue_session) {
            (void)session_manager.continue_latest();
        } else {
            (void)session_manager.new_session();
        }

        if (options.use_tui()) {
            return swe_agent::tui::run(session_manager, env_config.model_name);
        }

        swe_agent::agent::AgentRunOptions run_options;
        run_options.on_event = print_agent_event;
        const bool interactive_console = ::isatty(STDIN_FILENO) != 0;
        const swe_agent::agent::PolicyContext policy_context{
            .working_dir = session_manager.workspace(),
            .workspace_root = session_manager.workspace(),
        };
        run_options.authorizer = [policy_context, interactive_console](
                                     const swe_agent::agent::CommandRequest& request) {
            return swe_agent::agent::authorize_command(
                request,
                policy_context,
                false,
                [interactive_console](
                    const swe_agent::agent::CommandRequest& review_request) {
                    return swe_agent::app_cli::review_console_command(
                        review_request,
                        std::cin,
                        std::cerr,
                        interactive_console);
                });
        };
        (void)session_manager.submit(options.task, run_options);
        return 0;
    } catch (const CLI::ParseError& e) {
        return cli.exit(e);
    } catch (const std::exception& e) {
        std::cerr << "错误：" << e.what() << '\n';
        return 1;
    }
}
