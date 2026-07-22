#pragma once

#include "agent/agent_session.hpp"
#include "agent/session_store.hpp"
#include "config/agent_loader.hpp"
#include "model/model.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace swe_agent::acp {

/** @brief ACP Session 请求校验或恢复失败。 */
class AcpSessionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/** @brief 已加载到当前 ACP 进程的 Session。 */
struct AcpActiveSession {
    /** @brief 可提交 Prompt 的核心 Session。 */
    std::shared_ptr<agent::AgentSession> session;
    /** @brief 已校验并规范化的工作目录。 */
    std::string workspace;
};

/** @brief load 返回的活动 Session 与回放快照。 */
struct AcpLoadedSession {
    /** @brief 注册到当前连接的活动 Session。 */
    AcpActiveSession active;
    /** @brief 用于向 Client 回放可见历史的持久化快照。 */
    agent::SessionSnapshot snapshot;
};

/**
 * @brief 管理一条 ACP 连接内的多个活动 Session。
 *
 * 持久化由 ISessionStore 负责；Registry 只持有当前连接使用的内存上下文。
 */
class AcpSessionRegistry {
public:
    /** @brief 使用共享运行时依赖构造 Session Registry。 */
    AcpSessionRegistry(
        model::IProvider& provider,
        config::AgentConfig agent_config,
        agent::ISessionStore& session_store,
        std::string model_name);

    /** @brief 创建并注册新 Session。 */
    [[nodiscard]] AcpActiveSession create(std::string_view cwd);

    /** @brief 恢复 Session 并返回用于 Client 回放的快照。 */
    [[nodiscard]] AcpLoadedSession load(
        std::string_view session_id,
        std::string_view cwd);

    /** @brief 恢复 Session，但不返回历史回放。 */
    [[nodiscard]] AcpActiveSession resume(
        std::string_view session_id,
        std::string_view cwd);

    /** @brief 获取当前连接内已加载的 Session。 */
    [[nodiscard]] std::optional<AcpActiveSession> find(
        std::string_view session_id) const;

    /** @brief 从活动 Registry 释放 Session，不删除持久化数据。 */
    bool close(std::string_view session_id);

    /** @brief 查询持久化 Session 列表。 */
    [[nodiscard]] agent::SessionListPage list(
        const agent::SessionListQuery& query);

    /** @brief 校验并规范化 Session 工作目录。 */
    [[nodiscard]] static std::string canonical_workspace(std::string_view cwd);

private:
    [[nodiscard]] AcpLoadedSession restore(
        std::string_view session_id,
        std::string_view cwd);

    model::IProvider& provider_;
    config::AgentConfig agent_config_;
    agent::ISessionStore& session_store_;
    std::string model_name_;
    mutable std::mutex mutex_;
    std::unordered_map<agent::SessionId, AcpActiveSession> active_;
};

}  // namespace swe_agent::acp
