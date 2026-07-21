#include "agent/agent_session.hpp"
#include "agent/agent_loop.hpp"

#include <chrono>
#include <utility>

namespace swe_agent::agent {
namespace {

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

AgentSession::AgentSession(
    model::ModelClient& client,
    config::AgentConfig config
) : provider_(client),
    config_(std::move(config)) {
    clear();
}

AgentSession::AgentSession(
    model::IProvider& provider,
    config::AgentConfig config,
    ISessionStore& store,
    SessionSnapshot snapshot)
    : provider_(provider),
      config_(std::move(config)),
      store_(&store),
      metadata_(std::move(snapshot.metadata)) {
    history_.reserve(snapshot.messages.size());
    for (auto& message : snapshot.messages) {
        history_.push_back({
            .role = message.role,
            .content = std::move(message.content),
        });
    }
}

AgentSession AgentSession::create(
    model::IProvider& provider,
    config::AgentConfig config,
    ISessionStore& store,
    std::string workspace,
    std::string model_name) {
    SessionSnapshot snapshot = store.create_session({
        .workspace = std::move(workspace),
        .model_name = std::move(model_name),
        .system_prompt = config.system_prompt,
        .step_limit = config.step_limit,
    });
    return AgentSession{
        provider,
        std::move(config),
        store,
        std::move(snapshot),
    };
}

AgentSession AgentSession::restore(
    model::IProvider& provider,
    config::AgentConfig config,
    ISessionStore& store,
    SessionSnapshot snapshot) {
    if (snapshot.metadata.id.empty() || snapshot.messages.empty()) {
        throw SessionStorageError{"Invalid empty session snapshot"};
    }
    if (snapshot.messages.front().sequence != 0 ||
        snapshot.messages.front().role != model::Role::System ||
        snapshot.messages.front().kind != SessionMessageKind::System) {
        throw SessionStorageError{"Session snapshot has no System seed"};
    }
    for (std::size_t index = 0; index < snapshot.messages.size(); ++index) {
        if (snapshot.messages[index].sequence != index) {
            throw SessionStorageError{"Session snapshot sequence is not contiguous"};
        }
    }

    config.system_prompt = snapshot.metadata.system_prompt;
    return AgentSession{
        provider,
        std::move(config),
        store,
        std::move(snapshot),
    };
}

void AgentSession::clear() {
    if (store_ != nullptr) {
        store_->reset_session(metadata_.id, config_.system_prompt);
        metadata_.title.clear();
        metadata_.system_prompt = config_.system_prompt;
    }
    history_.clear();
    history_.push_back({
        .role = model::Role::System,
        .content = config_.system_prompt,
    });
}

AgentRunResult AgentSession::submit(
    std::string user_message,
    const AgentRunOptions& options) {
    HistoryHooks history_hooks;
    if (store_ != nullptr) {
        history_hooks.commit_append = [this](const HistoryAppend& append) {
            store_->append_message(metadata_.id, {
                .sequence = append.sequence,
                .role = append.message.role,
                .kind = append.kind,
                .content = append.message.content,
                .created_at_ms = now_ms(),
            });
        };
    }

    append_history(
        history_,
        {
            .role = model::Role::User,
            .content = std::move(user_message),
        },
        HistoryEntryKind::UserPrompt,
        history_hooks);
    return run(provider_, config_, history_, options, history_hooks);
}

const model::MSG& AgentSession::history() const noexcept {
    return history_;
}

const SessionId& AgentSession::id() const noexcept {
    return metadata_.id;
}

}  // namespace swe_agent::agent
