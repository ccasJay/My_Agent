#pragma once

#include "agent/session_store.hpp"

#include <filesystem>
#include <memory>

namespace swe_agent::agent {

/**
 * @brief 基于 SQLite 的 ISessionStore 实现（Pimpl 隐藏 sqlite3 细节）
 *
 * 路径通常由 session_database_path() 提供；limits 约束单条/整会话大小。
 * 不可拷贝，可移动；通过基类指针 delete 时依赖虚析构。
 */
class SqliteSessionStore final : public ISessionStore {
public:
    /**
     * @brief 打开或创建数据库并初始化 schema
     * @param database_path SQLite 文件路径
     * @param limits 消息/会话字节上限，默认 kMax*
     */
    explicit SqliteSessionStore(
        std::filesystem::path database_path,
        SessionStoreLimits limits = {});
    ~SqliteSessionStore() override;

    SqliteSessionStore(const SqliteSessionStore&) = delete;
    SqliteSessionStore& operator=(const SqliteSessionStore&) = delete;
    SqliteSessionStore(SqliteSessionStore&&) noexcept;
    SqliteSessionStore& operator=(SqliteSessionStore&&) noexcept;

    SessionSnapshot create_session(const SessionSeed& seed) override;
    void append_message(
        std::string_view id,
        const SessionMessage& message) override;
    [[nodiscard]] std::optional<SessionSnapshot> load_session(
        std::string_view id) override;
    [[nodiscard]] std::optional<SessionSnapshot> latest_session(
        std::string_view workspace) override;
    [[nodiscard]] std::vector<SessionSummary> list_sessions(
        std::string_view workspace,
        std::size_t limit) override;
    void reset_session(
        std::string_view id,
        std::string_view system_prompt) override;
    void update_model(
        std::string_view id,
        std::string_view model_name) override;
    void archive_session(std::string_view id) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace swe_agent::agent
