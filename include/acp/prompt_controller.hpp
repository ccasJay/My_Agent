#pragma once

#include "acp/json_rpc.hpp"
#include "acp/session_registry.hpp"
#include "agent/agent_event.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace swe_agent::acp {

/**
 * @brief 管理 ACP 连接内唯一运行中的 Prompt。
 *
 * Prompt 在 Worker 中执行；协议线程仍可投递取消和权限响应。
 */
class AcpPromptController {
public:
    /** @brief 构造绑定到指定 JSON-RPC 连接的 Prompt 控制器。 */
    explicit AcpPromptController(
        JsonRpcConnection& connection,
        std::chrono::milliseconds permission_timeout =
            std::chrono::minutes{5});

    /** @brief 请求停止并回收仍在运行的 Worker。 */
    ~AcpPromptController();

    AcpPromptController(const AcpPromptController&) = delete;
    AcpPromptController& operator=(const AcpPromptController&) = delete;

    /**
     * @brief 启动 Prompt。
     * @return 已启动返回 true；已有 Prompt 运行时返回 false。
     */
    bool start(
        Json request_id,
        std::string prompt,
        AcpActiveSession active_session);

    /** @brief 请求取消指定 Session 当前 Prompt。 */
    bool cancel(std::string_view session_id);

    /** @brief 取消指定 Prompt 并等待 Worker 退出。 */
    bool cancel_and_wait(std::string_view session_id);

    /** @brief 当前是否正在执行指定 Session 的 Prompt。 */
    [[nodiscard]] bool is_running(std::string_view session_id) const;

    /** @brief 投递 Client 对 Agent 反向请求的响应。 */
    bool handle_response(const Json& message);

    /** @brief 停止并回收所有 Worker。 */
    void stop_and_join();

private:
    struct PromptState;

    void run_prompt(
        const std::shared_ptr<PromptState>& state,
        std::string prompt,
        AcpActiveSession active_session);
    agent::CommandDecision authorize_command(
        const std::shared_ptr<PromptState>& state,
        const agent::CommandRequest& request,
        const std::string& workspace);
    agent::CommandDecision request_permission(
        const std::shared_ptr<PromptState>& state,
        const agent::CommandRequest& request,
        std::string_view tool_call_id);
    void handle_event(
        const std::shared_ptr<PromptState>& state,
        const agent::AgentEvent& event);
    void finalize_unfinished_tools(
        const std::shared_ptr<PromptState>& state,
        std::string_view message);
    void reap_finished();

    JsonRpcConnection& connection_;
    std::chrono::milliseconds permission_timeout_;
    mutable std::mutex mutex_;
    std::shared_ptr<PromptState> active_;
    std::thread worker_;
    std::atomic<std::uint64_t> next_tool_call_id_{1};
};

}  // namespace swe_agent::acp
