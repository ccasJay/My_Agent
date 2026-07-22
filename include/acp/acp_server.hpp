#pragma once

#include "acp/json_rpc.hpp"
#include "agent/session_store.hpp"
#include "config/agent_loader.hpp"
#include "model/model.hpp"

#include <string>
#include <string_view>

namespace swe_agent::acp {

/** @brief ACP Server 运行所需的核心依赖。 */
struct AcpServerContext {
    model::IProvider& provider;
    config::AgentConfig agent_config;
    agent::ISessionStore& session_store;
    std::string model_name;
};

/**
 * @brief ACP v1 Agent 端协议状态机。
 *
 * 主线程持续读取 JSON-RPC；耗时 Prompt 在独立 Worker 中执行。
 */
class AcpServer {
public:
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
    void send_invalid_request(const Json& id, std::string message);

    JsonRpcConnection& connection_;
    AcpServerContext context_;
    bool initialized_{false};
};

}  // namespace swe_agent::acp
