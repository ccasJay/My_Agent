#pragma once

#include "agent/history.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace swe_agent::agent {

using SessionId = std::string;

/// 单条消息内容上限（字节）
inline constexpr std::size_t kMaxSessionMessageBytes = 1024 * 1024;
/// 单会话累计内容上限（字节）
inline constexpr std::size_t kMaxSessionBytes = 64 * 1024 * 1024;

/**
 * @brief SessionStore 写入大小限制
 */
struct SessionStoreLimits {
    std::size_t max_message_bytes{kMaxSessionMessageBytes};
    std::size_t max_session_bytes{kMaxSessionBytes};
};

/// 持久化消息 kind，与 HistoryEntryKind 同型
using SessionMessageKind = HistoryEntryKind;

/**
 * @brief 会话元数据（不含消息正文）
 */
struct SessionMetadata {
    SessionId id;
    std::string title;
    std::string workspace;
    std::string model_name;
    std::string system_prompt;
    std::size_t step_limit{0};
    std::int64_t created_at_ms{0};
    std::int64_t updated_at_ms{0};
};

/**
 * @brief 持久化的单条会话消息
 *
 * @note sequence 须从 0 连续递增；0 号应为 System 种子
 */
struct SessionMessage {
    std::size_t sequence{0};
    model::Role role{model::Role::User};
    SessionMessageKind kind{SessionMessageKind::UserPrompt};
    std::string content;
    std::int64_t created_at_ms{0};
};

/**
 * @brief 完整会话快照：元数据 + 全部消息
 *
 * 用于 create / load / restore；比 SessionSummary 重。
 */
struct SessionSnapshot {
    SessionMetadata metadata;
    std::vector<SessionMessage> messages;
};

/**
 * @brief 会话列表用的轻量摘要（无消息正文）
 */
struct SessionSummary {
    SessionId id;
    std::string title;
    std::string workspace;
    std::string model_name;
    std::int64_t updated_at_ms{0};
};

/** @brief Session 列表的稳定键集游标。 */
struct SessionListCursor {
    /** @brief 游标指向记录的更新时间。 */
    std::int64_t updated_at_ms{0};
    /** @brief 在时间相同时用于稳定排序的 Session ID。 */
    SessionId id;
};

/** @brief Session 分页查询条件。 */
struct SessionListQuery {
    /** @brief 可选的规范化工作区过滤条件。 */
    std::optional<std::string> workspace;
    /** @brief 只返回严格早于该键集游标的记录。 */
    std::optional<SessionListCursor> before;
    /** @brief 本页最多返回的记录数。 */
    std::size_t limit{20};
};

/** @brief Session 分页查询结果。 */
struct SessionListPage {
    /** @brief 当前页的 Session 摘要。 */
    std::vector<SessionSummary> sessions;
    /** @brief 仍有下一页时返回的继续查询游标。 */
    std::optional<SessionListCursor> next_cursor;
};

/**
 * @brief 创建新会话时的种子字段
 */
struct SessionSeed {
    std::string workspace;
    std::string model_name;
    std::string system_prompt;
    std::size_t step_limit{0};
};

/**
 * @brief 会话持久化失败时抛出
 */
class SessionStorageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/**
 * @brief 会话持久化抽象接口
 *
 * 实现须保证 workspace 隔离；append 失败应向上抛 SessionStorageError
 *（或派生），由调用方（如 append_history）回滚内存。
 */
class ISessionStore {
public:
    virtual ~ISessionStore() = default;

    /**
     * @brief 创建会话并写入 System 种子消息（sequence=0）
     * @return 含 metadata 与初始 messages 的快照
     */
    virtual SessionSnapshot create_session(const SessionSeed& seed) = 0;

    /**
     * @brief 追加一条消息（sequence 须与调用方 history 下标一致）
     */
    virtual void append_message(
        std::string_view id,
        const SessionMessage& message) = 0;

    /**
     * @brief 按 id 加载完整快照
     * @return 不存在则为 nullopt
     */
    [[nodiscard]] virtual std::optional<SessionSnapshot> load_session(
        std::string_view id) = 0;

    /**
     * @brief 当前 workspace 下最近更新的会话
     */
    [[nodiscard]] virtual std::optional<SessionSnapshot> latest_session(
        std::string_view workspace) = 0;

    /**
     * @brief 列出 workspace 内会话摘要，按更新时间倒序
     * @param limit 最大条数
     */
    [[nodiscard]] virtual SessionListPage list_sessions_page(
        const SessionListQuery& query) = 0;

    /**
     * @brief 兼容现有 CLI/TUI 的按工作区列表接口。
     */
    [[nodiscard]] std::vector<SessionSummary> list_sessions(
        std::string_view workspace,
        std::size_t limit) {
        return list_sessions_page({
            .workspace = std::string{workspace},
            .limit = limit,
        }).sessions;
    }

    /**
     * @brief 清空消息并重置为新的 System 种子（保留 session id）
     */
    virtual void reset_session(
        std::string_view id,
        std::string_view system_prompt) = 0;

    /**
     * @brief 更新会话绑定的模型名
     */
    virtual void update_model(
        std::string_view id,
        std::string_view model_name) = 0;

    /**
     * @brief 归档会话（列表/latest 不再返回）
     */
    virtual void archive_session(std::string_view id) = 0;
};

}  // namespace swe_agent::agent
