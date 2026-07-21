#include <catch2/catch_test_macros.hpp>

#include "agent/session_manager.hpp"
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
        std::string response = std::move(responses.front());
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
              ("swe-agent-session-manager-" + std::to_string(
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

swe_agent::config::AgentConfig make_config() {
    return {
        .system_prompt = "system",
        .step_limit = 3,
    };
}

}  // namespace

TEST_CASE("session manager creates a new active session", "[session_manager]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    FakeProvider provider;
    swe_agent::agent::SessionManager manager{
        provider,
        make_config(),
        store,
        "/tmp/project",
        "test-model",
    };

    const std::string first_id = manager.new_session().id();
    const std::string second_id = manager.new_session().id();

    REQUIRE_FALSE(first_id.empty());
    REQUIRE_FALSE(second_id.empty());
    REQUIRE(first_id != second_id);
    REQUIRE(manager.active_session().id() == second_id);
}

TEST_CASE("session manager continues the latest workspace session", "[session_manager]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    FakeProvider provider;
    swe_agent::agent::SessionManager creator{
        provider,
        make_config(),
        store,
        "/tmp/project",
        "model-a",
    };
    const std::string session_id = creator.new_session().id();
    (void)store.create_session({
        .workspace = "/tmp/other",
        .model_name = "other-model",
        .system_prompt = "system",
        .step_limit = 3,
    });
    swe_agent::agent::SessionManager resumed{
        provider,
        make_config(),
        store,
        "/tmp/project",
        "model-b",
    };

    const auto& active = resumed.continue_latest();

    REQUIRE(active.id() == session_id);
    REQUIRE(store.load_session(session_id)->metadata.model_name == "model-b");
}

TEST_CASE("session manager reports missing session on continue", "[session_manager]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    FakeProvider provider;
    swe_agent::agent::SessionManager manager{
        provider,
        make_config(),
        store,
        "/tmp/project",
        "test-model",
    };

    REQUIRE_THROWS_AS(
        manager.continue_latest(),
        swe_agent::agent::SessionStorageError);
}

TEST_CASE("session manager resumes a session by unique id prefix", "[session_manager]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    FakeProvider provider;
    swe_agent::agent::SessionManager manager{
        provider,
        make_config(),
        store,
        "/tmp/project",
        "test-model",
    };
    const std::string first_id = manager.new_session().id();
    const std::string second_id = manager.new_session().id();

    const auto& resumed = manager.resume(first_id.substr(0, 8));

    REQUIRE(resumed.id() == first_id);
    REQUIRE(resumed.id() != second_id);
    REQUIRE_THROWS_AS(
        manager.resume("short"),
        swe_agent::agent::SessionStorageError);
    REQUIRE_THROWS_AS(
        manager.resume("ffffffff"),
        swe_agent::agent::SessionStorageError);
}

TEST_CASE("session manager lists current workspace sessions", "[session_manager]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    FakeProvider provider;
    swe_agent::agent::SessionManager manager{
        provider,
        make_config(),
        store,
        "/tmp/project",
        "test-model",
    };
    (void)manager.new_session();
    (void)manager.new_session();
    (void)store.create_session({
        .workspace = "/tmp/other",
        .model_name = "other-model",
        .system_prompt = "system",
        .step_limit = 3,
    });

    const auto sessions = manager.list_sessions();

    REQUIRE(sessions.size() == 2);
    REQUIRE(sessions[0].workspace == "/tmp/project");
    REQUIRE(sessions[1].workspace == "/tmp/project");
}

TEST_CASE("session manager submits through the active session", "[session_manager]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    FakeProvider provider;
    provider.responses = {
        "Conclusion: done\nRUN: echo COMPLETE_TASK\n",
    };
    swe_agent::agent::SessionManager manager{
        provider,
        make_config(),
        store,
        "/tmp/project",
        "test-model",
    };
    const std::string id = manager.new_session().id();

    const auto result = manager.submit("task");

    REQUIRE(result.status == swe_agent::agent::AgentRunStatus::Completed);
    REQUIRE(provider.seen_histories.size() == 1);
    REQUIRE(store.load_session(id)->messages.size() == 3);
}

TEST_CASE("session manager exposes the active persisted snapshot", "[session_manager]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    FakeProvider provider;
    swe_agent::agent::SessionManager manager{
        provider,
        make_config(),
        store,
        "/tmp/project",
        "test-model",
    };
    const std::string id = manager.new_session().id();

    const auto snapshot = manager.active_snapshot();

    REQUIRE(snapshot.metadata.id == id);
    REQUIRE(snapshot.messages.size() == 1);
    REQUIRE(snapshot.messages.front().kind ==
            swe_agent::agent::SessionMessageKind::System);
}
