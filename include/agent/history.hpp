#pragma once

#include "model/message.hpp"

#include <cstddef>
#include <functional>
#include <utility>

namespace swe_agent::agent {

/**
 * @brief 历史条目语义类型（与持久化 SessionMessageKind 对齐）
 */
enum class HistoryEntryKind {
    System,
    UserPrompt,
    Assistant,
    Observation,
    HostHint,
};

/**
 * @brief 一次 history 追加的描述，供 hooks / 存储层使用
 *
 * @note sequence 为写入后在 history 中的下标（0-based）
 */
struct HistoryAppend {
    std::size_t sequence{0};
    HistoryEntryKind kind{HistoryEntryKind::UserPrompt};
    model::Message message;
};

/**
 * @brief 可选的 history 副作用钩子
 *
 * commit_append 在消息已 push 到内存 history 后调用；
 * 用于同步写入 SessionStore 等。空 function 表示不持久化。
 */
struct HistoryHooks {
    std::function<void(const HistoryAppend&)> commit_append;
};

/**
 * @brief 将消息追加到 history，并可选触发持久化 hook
 *
 * 顺序：先 push_back，再调用 commit_append。
 * 若 commit_append 抛异常，会 pop_back 回滚内存 history 后重新抛出，
 * 保证内存与存储失败时的一致性（尽力而为）。
 *
 * @param history 调用方持有的会话消息列表
 * @param message 待追加消息（按值移动）
 * @param kind 条目语义类型
 * @param hooks 可选；commit_append 为空则只改内存
 */
inline void append_history(
    model::MSG& history,
    model::Message message,
    HistoryEntryKind kind,
    const HistoryHooks& hooks = {}) {
    history.push_back(std::move(message));
    if (!hooks.commit_append) {
        return;
    }

    try {
        hooks.commit_append(HistoryAppend{
            .sequence = history.size() - 1,
            .kind = kind,
            .message = history.back(),
        });
    } catch (...) {
        history.pop_back();
        throw;
    }
}

}  // namespace swe_agent::agent
