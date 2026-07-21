#pragma once

#include "agent/agent_session.hpp"

#include <cstddef>
#include <memory>
#include <string>

namespace swe_agent::agent {

/**
 * @brief 面向 CLI/TUI 的会话编排：new / continue / resume / list
 *
 * 持有当前 active AgentSession，委托 ISessionStore 做持久化。
 */
class SessionManager {
public:
    /**
     * @brief 绑定 provider、配置、store 与当前 workspace/model
     * @note 构造后不会自动创建会话；先 new_session / continue_latest / resume
     */
    SessionManager(
        model::IProvider& provider,
        config::AgentConfig config,
        ISessionStore& store,
        std::string workspace,
        std::string model_name);

    /**
     * @brief 创建新会话并设为 active（替换内存中的上一会话指针）
     */
    AgentSession& new_session();

    /**
     * @brief 恢复 workspace 下最新会话并设为 active
     * @throws SessionStorageError 无历史会话或加载失败
     */
    AgentSession& continue_latest();

    /**
     * @brief 按 id 前缀匹配并 resume（前缀至少 8 字符）
     * @throws SessionStorageError 无匹配、前缀歧义或加载失败
     */
    AgentSession& resume(std::string_view id_prefix);

    /**
     * @brief 列出当前 workspace 会话摘要
     * @param limit 默认 20
     */
    [[nodiscard]] std::vector<SessionSummary> list_sessions(
        std::size_t limit = 20) const;

    /**
     * @brief 从 store 再读当前 active 的完整快照
     * @throws SessionStorageError 存储中已不存在；std::logic_error 无 active
     */
    [[nodiscard]] SessionSnapshot active_snapshot() const;

    /**
     * @brief 向 active 会话 submit 用户消息
     */
    AgentRunResult submit(
        std::string user_message,
        const AgentRunOptions& options = {});

    [[nodiscard]] AgentSession& active_session();
    [[nodiscard]] const AgentSession& active_session() const;

private:
    model::IProvider& provider_;
    config::AgentConfig config_;
    ISessionStore& store_;
    std::string workspace_;
    std::string model_name_;
    std::unique_ptr<AgentSession> active_;
};

}  // namespace swe_agent::agent
