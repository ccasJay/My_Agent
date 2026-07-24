#pragma once

#include <functional>
#include <string>

namespace swe_agent::agent {
class SessionManager;
}

namespace swe_agent::tui {

class TuiSession;

/**
 * @brief 斜杠 handler 的依赖注入上下文。
 *
 * 不包含 FTXUI 类型；会话切换后的日志缓存失效由 on_session_view_changed 处理。
 */
struct SlashContext {
    agent::SessionManager& sessions;
    TuiSession& ui;

    /** 会话视图变更后由 host 使日志缓存失效。 */
    std::function<void()> on_session_view_changed;

    void notice(std::string heading, std::string content, bool error = false);
    /** 标题固定为 "Session command"。 */
    void notice_error(std::string content);
    /**
     * @brief 重新加载 active session 快照到 TUI。
     * @return load_session 是否成功；成功时触发 on_session_view_changed。
     */
    bool reload_active_session();
};

}  // namespace swe_agent::tui
