#pragma once

#include "agent/agent_event.hpp"
#include "agent/agent_run_result.hpp"
#include "agent/session_store.hpp"
#include "config/agent_loader.hpp"
#include "model/message.hpp"
#include "model/model.hpp"

#include <string>

namespace swe_agent::agent {

/**
 * @brief 单次 Agent 会话：内存 history + 可选持久化
 *
 * 无 store 时仅内存（旧构造）；有 store 时 submit 经 HistoryHooks 追加落库。
 * UI 线程模型仍由 tui::TuiSession 负责。
 */
class AgentSession {
public:
    /**
     * @brief 仅内存会话（不绑定 ISessionStore）
     */
    AgentSession(
        model::ModelClient& client,
        config::AgentConfig config);

    /**
     * @brief 在 store 中 create_session 并构造可持久化会话
     */
    [[nodiscard]] static AgentSession create(
        model::IProvider& provider,
        config::AgentConfig config,
        ISessionStore& store,
        std::string workspace,
        std::string model_name);

    /**
     * @brief 从已有快照恢复会话
     * @throws SessionStorageError 快照为空、无 System 种子或 sequence 不连续
     */
    [[nodiscard]] static AgentSession restore(
        model::IProvider& provider,
        config::AgentConfig config,
        ISessionStore& store,
        SessionSnapshot snapshot);

    /**
     * @brief 追加用户消息并跑 agent loop
     *
     * 若绑定了 store，通过 HistoryHooks 将 User/Assistant/Observation 等写入库。
     */
    AgentRunResult submit(
        std::string user_message,
        const AgentRunOptions& options = {});

    /**
     * @brief 清空 history；有 store 时 reset_session 并保留 session id
     */
    void clear();

    [[nodiscard]]
    const model::MSG& history() const noexcept;

    [[nodiscard]] const SessionId& id() const noexcept;

private:
    AgentSession(
        model::IProvider& provider,
        config::AgentConfig config,
        ISessionStore& store,
        SessionSnapshot snapshot);

    model::IProvider& provider_;
    config::AgentConfig config_;
    ISessionStore* store_{nullptr};
    SessionMetadata metadata_;
    model::MSG history_;
};

}  // namespace swe_agent::agent
