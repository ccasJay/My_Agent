#pragma once

#include "acp/json_rpc.hpp"
#include "acp/prompt_controller.hpp"
#include "acp/session_registry.hpp"
#include "agent/session_store.hpp"
#include "config/agent_loader.hpp"
#include "model/model.hpp"

#include <string>
#include <string_view>

namespace swe_agent::acp {

/** @brief ACP Server 运行所需的核心依赖。 */
struct AcpServerContext {
    /** @brief 执行模型查询的 Provider。 */
    model::IProvider& provider;
    /** @brief 新建或恢复 Session 使用的 Agent 配置。 */
    config::AgentConfig agent_config;
    /** @brief ACP 与现有前端共享的持久化存储。 */
    agent::ISessionStore& session_store;
    /** @brief 本进程实际使用的模型名。 */
    std::string model_name;
};

/**
 * @brief ACP v1 Agent 端协议状态机。
 *
 * 主线程持续读取 JSON-RPC；耗时 Prompt 在独立 Worker 中执行。
 */
class AcpServer {
public:
    /** @brief 使用给定连接和运行时依赖构造协议服务。 */
    AcpServer(JsonRpcConnection& connection, AcpServerContext context);

    /** @brief 运行协议循环直到输入流关闭。 */
    int run();

private:
    void dispatch(const Json& message);
    void handle_request(
        std::string_view method,
        const Json& params,
        const Json& id);
    void handle_notification(std::string_view method, const Json& params);
    void handle_peer_response(const Json& message);
    void handle_initialize(const Json& params, const Json& id);
    void handle_new_session(const Json& params, const Json& id);
    void handle_list_sessions(const Json& params, const Json& id);
    void handle_load_session(const Json& params, const Json& id);
    void handle_resume_session(const Json& params, const Json& id);
    void handle_close_session(const Json& params, const Json& id);
    void handle_prompt(const Json& params, const Json& id);
    void replay_history(const agent::SessionSnapshot& snapshot);
    void send_invalid_request(const Json& id, std::string message);

    JsonRpcConnection& connection_;
    AcpServerContext context_;
    AcpSessionRegistry sessions_;
    AcpPromptController prompts_;
    bool initialized_{false};
};

}  // namespace swe_agent::acp
