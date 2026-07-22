#include "acp/session_registry.hpp"

#include <filesystem>
#include <utility>

namespace swe_agent::acp {

AcpSessionRegistry::AcpSessionRegistry(
    model::IProvider& provider,
    config::AgentConfig agent_config,
    agent::ISessionStore& session_store,
    std::string model_name,
    std::size_t max_active_sessions)
    : provider_(provider),
      agent_config_(std::move(agent_config)),
      session_store_(session_store),
      model_name_(std::move(model_name)),
      max_active_sessions_(max_active_sessions) {}

AcpActiveSession AcpSessionRegistry::create(std::string_view cwd) {
    {
        std::lock_guard lock{mutex_};
        if (active_.size() >= max_active_sessions_) {
            throw AcpSessionCapacityError{
                "Active session limit reached"};
        }
    }
    const std::string workspace = canonical_workspace(cwd);
    auto session = std::make_shared<agent::AgentSession>(
        agent::AgentSession::create(
            provider_,
            agent_config_,
            session_store_,
            workspace,
            model_name_));
    AcpActiveSession active{
        .session = std::move(session),
        .workspace = workspace,
    };
    {
        std::lock_guard lock{mutex_};
        active_[active.session->id()] = active;
    }
    return active;
}

AcpLoadedSession AcpSessionRegistry::load(
    std::string_view session_id,
    std::string_view cwd) {
    return restore(session_id, cwd);
}

AcpActiveSession AcpSessionRegistry::resume(
    std::string_view session_id,
    std::string_view cwd) {
    return restore(session_id, cwd).active;
}

std::optional<AcpActiveSession> AcpSessionRegistry::find(
    std::string_view session_id) const {
    std::lock_guard lock{mutex_};
    const auto found = active_.find(std::string{session_id});
    if (found == active_.end()) {
        return std::nullopt;
    }
    return found->second;
}

bool AcpSessionRegistry::close(std::string_view session_id) {
    std::lock_guard lock{mutex_};
    return active_.erase(std::string{session_id}) > 0;
}

agent::SessionListPage AcpSessionRegistry::list(
    const agent::SessionListQuery& query) {
    return session_store_.list_sessions_page(query);
}

std::string AcpSessionRegistry::canonical_workspace(std::string_view cwd) {
    const std::filesystem::path requested{cwd};
    if (cwd.empty() || !requested.is_absolute()) {
        throw AcpSessionError{"Session cwd must be an absolute path"};
    }

    std::error_code error;
    const std::filesystem::path canonical =
        std::filesystem::canonical(requested, error);
    if (error || !std::filesystem::is_directory(canonical, error) || error) {
        throw AcpSessionError{"Session cwd must be an existing directory"};
    }
    return canonical.string();
}

AcpLoadedSession AcpSessionRegistry::restore(
    std::string_view session_id,
    std::string_view cwd) {
    {
        std::lock_guard lock{mutex_};
        if (!active_.contains(std::string{session_id}) &&
            active_.size() >= max_active_sessions_) {
            throw AcpSessionCapacityError{
                "Active session limit reached"};
        }
    }
    const std::string workspace = canonical_workspace(cwd);
    auto snapshot = session_store_.load_session(session_id);
    if (!snapshot.has_value()) {
        throw AcpSessionError{"Session not found"};
    }
    if (snapshot->metadata.workspace != workspace) {
        throw AcpSessionError{"Session cwd does not match persisted workspace"};
    }

    session_store_.update_model(session_id, model_name_);
    snapshot->metadata.model_name = model_name_;
    agent::SessionSnapshot replay_snapshot = *snapshot;
    auto session = std::make_shared<agent::AgentSession>(
        agent::AgentSession::restore(
            provider_,
            agent_config_,
            session_store_,
            std::move(*snapshot)));
    AcpActiveSession active{
        .session = std::move(session),
        .workspace = workspace,
    };
    {
        std::lock_guard lock{mutex_};
        active_[active.session->id()] = active;
    }
    return {
        .active = std::move(active),
        .snapshot = std::move(replay_snapshot),
    };
}

}  // namespace swe_agent::acp
