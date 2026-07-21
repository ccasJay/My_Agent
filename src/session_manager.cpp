#include "agent/session_manager.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace swe_agent::agent {

SessionManager::SessionManager(
    model::IProvider& provider,
    config::AgentConfig config,
    ISessionStore& store,
    std::string workspace,
    std::string model_name)
    : provider_(provider),
      config_(std::move(config)),
      store_(store),
      workspace_(std::move(workspace)),
      model_name_(std::move(model_name)) {}

AgentSession& SessionManager::new_session() {
    active_ = std::make_unique<AgentSession>(AgentSession::create(
        provider_,
        config_,
        store_,
        workspace_,
        model_name_));
    return *active_;
}

AgentSession& SessionManager::continue_latest() {
    const auto latest = store_.latest_session(workspace_);
    if (!latest.has_value()) {
        throw SessionStorageError{
            "No previous session found for the current workspace"};
    }

    store_.update_model(latest->metadata.id, model_name_);
    auto refreshed = store_.load_session(latest->metadata.id);
    if (!refreshed.has_value()) {
        throw SessionStorageError{"Session disappeared while restoring"};
    }
    active_ = std::make_unique<AgentSession>(AgentSession::restore(
        provider_,
        config_,
        store_,
        std::move(*refreshed)));
    return *active_;
}

AgentSession& SessionManager::resume(std::string_view id_prefix) {
    if (id_prefix.size() < 8) {
        throw SessionStorageError{"Session id prefix must contain at least 8 characters"};
    }

    const auto sessions = store_.list_sessions(
        workspace_,
        static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()));
    const SessionSummary* match = nullptr;
    for (const auto& session : sessions) {
        if (!session.id.starts_with(id_prefix)) {
            continue;
        }
        if (match != nullptr) {
            throw SessionStorageError{"Session id prefix is ambiguous"};
        }
        match = &session;
    }
    if (match == nullptr) {
        throw SessionStorageError{"Session not found in the current workspace"};
    }

    store_.update_model(match->id, model_name_);
    auto snapshot = store_.load_session(match->id);
    if (!snapshot.has_value()) {
        throw SessionStorageError{"Session disappeared while restoring"};
    }
    active_ = std::make_unique<AgentSession>(AgentSession::restore(
        provider_,
        config_,
        store_,
        std::move(*snapshot)));
    return *active_;
}

std::vector<SessionSummary> SessionManager::list_sessions(
    std::size_t limit) const {
    return store_.list_sessions(workspace_, limit);
}

SessionSnapshot SessionManager::active_snapshot() const {
    const auto snapshot = store_.load_session(active_session().id());
    if (!snapshot.has_value()) {
        throw SessionStorageError{"Active session disappeared from storage"};
    }
    return *snapshot;
}

AgentRunResult SessionManager::submit(
    std::string user_message,
    const AgentRunOptions& options) {
    return active_session().submit(std::move(user_message), options);
}

AgentSession& SessionManager::active_session() {
    if (!active_) {
        throw std::logic_error{"No active session"};
    }
    return *active_;
}

const AgentSession& SessionManager::active_session() const {
    if (!active_) {
        throw std::logic_error{"No active session"};
    }
    return *active_;
}

}  // namespace swe_agent::agent
