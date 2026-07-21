#include <catch2/catch_test_macros.hpp>

#include "agent/agent_session.hpp"
#include "agent/sqlite_session_store.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace {

class FakeProvider final : public swe_agent::model::IProvider {
public:
    swe_agent::model::ModelResponse query(
        const swe_agent::model::MSG& messages) override {
        seen_histories.push_back(messages);
        if (responses.empty()) {
            return {};
        }
        auto response = responses.front();
        responses.erase(responses.begin());
        return {std::move(response)};
    }

    std::vector<std::string> responses;
    std::vector<swe_agent::model::MSG> seen_histories;
};

class TempDatabase {
public:
    TempDatabase()
        : directory_(
              std::filesystem::temp_directory_path() /
              ("swe-agent-agent-session-" + std::to_string(
                  std::chrono::steady_clock::now()
                      .time_since_epoch()
                      .count()))),
          path_(directory_ / "agent.db") {
        std::filesystem::create_directories(directory_);
    }

    ~TempDatabase() {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path directory_;
    std::filesystem::path path_;
};

swe_agent::model::ModelClient make_client() {
    return swe_agent::model::ModelClient{
        {
            .base_url = "https://example.invalid/v1/chat/completions",
            .api_key = "test-key",
            .model_name = "test-model",
        },
    };
}

swe_agent::config::AgentConfig make_config() {
    return {
        .system_prompt = "system instructions",
        .step_limit = 3,
    };
}

void require_seed_history(
    const swe_agent::agent::AgentSession& session,
    std::string_view system_prompt) {
    const auto& history = session.history();
    REQUIRE(history.size() == 1);
    REQUIRE(history.front().role == swe_agent::model::Role::System);
    REQUIRE(history.front().content == system_prompt);
}

}  // namespace

TEST_CASE("agent session seeds history with its system prompt", "[agent_session]") {
    auto client = make_client();
    const auto config = make_config();

    const swe_agent::agent::AgentSession session{client, config};

    require_seed_history(session, config.system_prompt);
}

TEST_CASE("agent session clear restores its initial history", "[agent_session]") {
    auto client = make_client();
    const auto config = make_config();
    swe_agent::agent::AgentSession session{client, config};

    session.clear();

    require_seed_history(session, config.system_prompt);
}

TEST_CASE("persistent agent session creates its seed history", "[agent_session]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    FakeProvider provider;
    const auto config = make_config();

    auto session = swe_agent::agent::AgentSession::create(
        provider,
        config,
        store,
        "/tmp/project",
        "test-model");

    REQUIRE_FALSE(session.id().empty());
    require_seed_history(session, config.system_prompt);
    const auto persisted = store.load_session(session.id());
    REQUIRE(persisted.has_value());
    REQUIRE(persisted->messages.size() == 1);
    REQUIRE(persisted->messages.front().content == config.system_prompt);
}

TEST_CASE("persistent agent session checkpoints submit history", "[agent_session]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    FakeProvider provider;
    provider.responses = {
        "Conclusion: done\nRUN: echo COMPLETE_TASK\n",
    };
    auto session = swe_agent::agent::AgentSession::create(
        provider,
        make_config(),
        store,
        "/tmp/project",
        "test-model");

    const auto result = session.submit("fix the parser");

    REQUIRE(result.status == swe_agent::agent::AgentRunStatus::Completed);
    REQUIRE(provider.seen_histories.size() == 1);
    REQUIRE(provider.seen_histories.front().size() == 2);
    const auto persisted = store.load_session(session.id());
    REQUIRE(persisted.has_value());
    REQUIRE(persisted->messages.size() == 3);
    REQUIRE(persisted->messages[1].kind ==
            swe_agent::agent::SessionMessageKind::UserPrompt);
    REQUIRE(persisted->messages[1].content == "fix the parser");
    REQUIRE(persisted->messages[2].kind ==
            swe_agent::agent::SessionMessageKind::Assistant);
}

TEST_CASE("persistent agent session restores and continues history", "[agent_session]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    FakeProvider first_provider;
    first_provider.responses = {
        "Conclusion: first done\nRUN: echo COMPLETE_TASK\n",
    };
    auto first = swe_agent::agent::AgentSession::create(
        first_provider,
        make_config(),
        store,
        "/tmp/project",
        "test-model");
    (void)first.submit("first task");
    const auto snapshot = store.load_session(first.id());
    REQUIRE(snapshot.has_value());

    FakeProvider resumed_provider;
    resumed_provider.responses = {
        "Conclusion: second done\nRUN: echo COMPLETE_TASK\n",
    };
    auto resumed = swe_agent::agent::AgentSession::restore(
        resumed_provider,
        make_config(),
        store,
        *snapshot);
    const auto result = resumed.submit("second task");

    REQUIRE(result.status == swe_agent::agent::AgentRunStatus::Completed);
    REQUIRE(resumed_provider.seen_histories.size() == 1);
    REQUIRE(resumed_provider.seen_histories.front().size() == 4);
    const auto persisted = store.load_session(resumed.id());
    REQUIRE(persisted.has_value());
    REQUIRE(persisted->messages.size() == 5);
    REQUIRE(persisted->messages.front().kind ==
            swe_agent::agent::SessionMessageKind::System);
    REQUIRE(persisted->messages[3].content == "second task");
}

TEST_CASE("restored session uses the current step limit", "[agent_session]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    FakeProvider creator_provider;
    auto persisted_config = make_config();
    persisted_config.step_limit = 1;
    auto original = swe_agent::agent::AgentSession::create(
        creator_provider,
        persisted_config,
        store,
        "/tmp/project",
        "test-model");
    const auto snapshot = store.load_session(original.id());
    REQUIRE(snapshot.has_value());

    FakeProvider restored_provider;
    restored_provider.responses = {
        "response without a run command",
        "another response without a run command",
    };
    auto current_config = make_config();
    current_config.step_limit = 2;
    auto restored = swe_agent::agent::AgentSession::restore(
        restored_provider,
        current_config,
        store,
        *snapshot);

    const auto result = restored.submit("task");

    REQUIRE(result.status ==
            swe_agent::agent::AgentRunStatus::StepLimitReached);
    REQUIRE(result.step == 2);
    REQUIRE(restored_provider.seen_histories.size() == 2);
}

TEST_CASE("persistent agent session clears memory and storage", "[agent_session]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    FakeProvider provider;
    provider.responses = {
        "Conclusion: done\nRUN: echo COMPLETE_TASK\n",
    };
    auto session = swe_agent::agent::AgentSession::create(
        provider,
        make_config(),
        store,
        "/tmp/project",
        "test-model");
    (void)session.submit("task");

    session.clear();

    require_seed_history(session, "system instructions");
    const auto persisted = store.load_session(session.id());
    REQUIRE(persisted.has_value());
    REQUIRE(persisted->messages.size() == 1);
    REQUIRE(persisted->messages.front().content == "system instructions");
}
