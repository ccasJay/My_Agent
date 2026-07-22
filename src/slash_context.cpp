#include "tui/slash_context.hpp"

#include "agent/session_manager.hpp"
#include "tui/tui_session.hpp"

namespace swe_agent::tui {

void SlashContext::notice(
    std::string heading,
    std::string content,
    bool error) {
    ui.append_notice(std::move(heading), std::move(content), error);
}

void SlashContext::notice_error(std::string content) {
    notice("Session command", std::move(content), true);
}

bool SlashContext::reload_active_session() {
    if (!ui.load_session(sessions.active_snapshot())) {
        return false;
    }
    if (on_session_view_changed) {
        on_session_view_changed();
    }
    return true;
}

}  // namespace swe_agent::tui
