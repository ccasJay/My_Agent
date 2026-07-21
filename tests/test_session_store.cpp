#include <catch2/catch_test_macros.hpp>

#include "agent/sqlite_session_store.hpp"
#include "agent/session_paths.hpp"

#include <sqlite3.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

namespace {

class TempDatabase {
public:
    TempDatabase()
        : directory_(
              std::filesystem::temp_directory_path() /
              ("swe-agent-session-store-" + std::to_string(
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

}  // namespace

TEST_CASE("sqlite store creates and reloads a session", "[session_store]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};

    const auto created = store.create_session({
        .workspace = "/tmp/project",
        .model_name = "test-model",
        .system_prompt = "system instructions",
        .step_limit = 5,
    });
    const auto loaded = store.load_session(created.metadata.id);

    REQUIRE_FALSE(created.metadata.id.empty());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->metadata.workspace == "/tmp/project");
    REQUIRE(loaded->messages.size() == 1);
    REQUIRE(loaded->messages.front().sequence == 0);
    REQUIRE(
        loaded->messages.front().kind ==
        swe_agent::agent::SessionMessageKind::System);
    REQUIRE(loaded->messages.front().content == "system instructions");
}

TEST_CASE("sqlite store appends ordered messages", "[session_store]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    const auto created = store.create_session({
        .workspace = "/tmp/project",
        .model_name = "test-model",
        .system_prompt = "system instructions",
        .step_limit = 5,
    });

    store.append_message(created.metadata.id, {
        .sequence = 1,
        .role = swe_agent::model::Role::User,
        .kind = swe_agent::agent::SessionMessageKind::UserPrompt,
        .content = "fix the parser\nand add tests",
        .created_at_ms = 123,
    });

    const auto loaded = store.load_session(created.metadata.id);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->metadata.title == "fix the parser");
    REQUIRE(loaded->messages.size() == 2);
    REQUIRE(loaded->messages.back().sequence == 1);
    REQUIRE(loaded->messages.back().created_at_ms == 123);
    REQUIRE(loaded->messages.back().content == "fix the parser\nand add tests");
}

TEST_CASE("sqlite store truncates titles on a UTF-8 boundary", "[session_store]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    const auto created = store.create_session({
        .workspace = "/tmp/project",
        .model_name = "test-model",
        .system_prompt = "system",
        .step_limit = 3,
    });
    std::string long_title;
    std::string expected_title;
    for (int index = 0; index < 30; ++index) {
        long_title += "会";
        if (index < 26) {
            expected_title += "会";
        }
    }

    store.append_message(created.metadata.id, {
        .sequence = 1,
        .role = swe_agent::model::Role::User,
        .kind = swe_agent::agent::SessionMessageKind::UserPrompt,
        .content = long_title,
        .created_at_ms = 123,
    });

    REQUIRE(store.load_session(created.metadata.id)->metadata.title ==
            expected_title);
}

TEST_CASE("sqlite store lists the latest session per workspace", "[session_store]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    const auto first = store.create_session({
        .workspace = "/tmp/project",
        .model_name = "model-a",
        .system_prompt = "system",
        .step_limit = 3,
    });
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
    const auto second = store.create_session({
        .workspace = "/tmp/project",
        .model_name = "model-b",
        .system_prompt = "system",
        .step_limit = 3,
    });
    (void)store.create_session({
        .workspace = "/tmp/other",
        .model_name = "other-model",
        .system_prompt = "system",
        .step_limit = 3,
    });

    const auto latest = store.latest_session("/tmp/project");
    const auto sessions = store.list_sessions("/tmp/project", 20);

    REQUIRE(latest.has_value());
    REQUIRE(latest->metadata.id == second.metadata.id);
    REQUIRE(sessions.size() == 2);
    REQUIRE(sessions[0].id == second.metadata.id);
    REQUIRE(sessions[1].id == first.metadata.id);
}

TEST_CASE("sqlite store excludes archived sessions", "[session_store]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    const auto first = store.create_session({
        .workspace = "/tmp/project",
        .model_name = "model-a",
        .system_prompt = "system",
        .step_limit = 3,
    });
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
    const auto second = store.create_session({
        .workspace = "/tmp/project",
        .model_name = "model-b",
        .system_prompt = "system",
        .step_limit = 3,
    });

    store.archive_session(second.metadata.id);

    REQUIRE_FALSE(store.load_session(second.metadata.id).has_value());
    const auto latest = store.latest_session("/tmp/project");
    REQUIRE(latest.has_value());
    REQUIRE(latest->metadata.id == first.metadata.id);
}

TEST_CASE("sqlite store resets a session to one system message", "[session_store]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    const auto created = store.create_session({
        .workspace = "/tmp/project",
        .model_name = "model-a",
        .system_prompt = "old system",
        .step_limit = 3,
    });
    store.append_message(created.metadata.id, {
        .sequence = 1,
        .role = swe_agent::model::Role::User,
        .kind = swe_agent::agent::SessionMessageKind::UserPrompt,
        .content = "old task",
        .created_at_ms = 123,
    });

    store.reset_session(created.metadata.id, "new system");

    const auto loaded = store.load_session(created.metadata.id);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->metadata.id == created.metadata.id);
    REQUIRE(loaded->metadata.system_prompt == "new system");
    REQUIRE(loaded->metadata.title.empty());
    REQUIRE(loaded->messages.size() == 1);
    REQUIRE(loaded->messages.front().kind ==
            swe_agent::agent::SessionMessageKind::System);
    REQUIRE(loaded->messages.front().content == "new system");
}

TEST_CASE("sqlite store rejects an oversized message atomically", "[session_store]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    const auto created = store.create_session({
        .workspace = "/tmp/project",
        .model_name = "model-a",
        .system_prompt = "system",
        .step_limit = 3,
    });
    const std::string oversized(
        swe_agent::agent::kMaxSessionMessageBytes + 1,
        'x');

    REQUIRE_THROWS_AS(
        store.append_message(created.metadata.id, {
            .sequence = 1,
            .role = swe_agent::model::Role::Assistant,
            .kind = swe_agent::agent::SessionMessageKind::Assistant,
            .content = oversized,
            .created_at_ms = 123,
        }),
        swe_agent::agent::SessionStorageError);

    const auto loaded = store.load_session(created.metadata.id);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->messages.size() == 1);
}

TEST_CASE("sqlite store rejects a noncontiguous message sequence", "[session_store]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    const auto created = store.create_session({
        .workspace = "/tmp/project",
        .model_name = "model-a",
        .system_prompt = "system",
        .step_limit = 3,
    });

    REQUIRE_THROWS_AS(
        store.append_message(created.metadata.id, {
            .sequence = 2,
            .role = swe_agent::model::Role::User,
            .kind = swe_agent::agent::SessionMessageKind::UserPrompt,
            .content = "skipped sequence one",
            .created_at_ms = 123,
        }),
        swe_agent::agent::SessionStorageError);
    REQUIRE(store.load_session(created.metadata.id)->messages.size() == 1);
}

TEST_CASE("session database path honors SWE_AGENT_DATA_DIR", "[session_store]") {
    const char* previous = std::getenv("SWE_AGENT_DATA_DIR");
    const std::string saved = previous == nullptr ? std::string{} : previous;
    setenv("SWE_AGENT_DATA_DIR", "/tmp/custom-swe-agent-data", 1);

    const auto path = swe_agent::agent::session_database_path();

    if (previous == nullptr) {
        unsetenv("SWE_AGENT_DATA_DIR");
    } else {
        setenv("SWE_AGENT_DATA_DIR", saved.c_str(), 1);
    }
    REQUIRE(path ==
            std::filesystem::path{"/tmp/custom-swe-agent-data"} / "agent.db");
}

TEST_CASE("sqlite store enforces the total session size", "[session_store]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{
        database.path(),
        {
            .max_message_bytes = 64,
            .max_session_bytes = 20,
        },
    };
    const auto created = store.create_session({
        .workspace = "/tmp/project",
        .model_name = "model-a",
        .system_prompt = "system",
        .step_limit = 3,
    });

    REQUIRE_THROWS_AS(
        store.append_message(created.metadata.id, {
            .sequence = 1,
            .role = swe_agent::model::Role::User,
            .kind = swe_agent::agent::SessionMessageKind::UserPrompt,
            .content = "123456789012345",
            .created_at_ms = 123,
        }),
        swe_agent::agent::SessionStorageError);
    REQUIRE(store.load_session(created.metadata.id)->messages.size() == 1);
}

TEST_CASE("sqlite store updates the active model", "[session_store]") {
    TempDatabase database;
    swe_agent::agent::SqliteSessionStore store{database.path()};
    const auto created = store.create_session({
        .workspace = "/tmp/project",
        .model_name = "model-a",
        .system_prompt = "system",
        .step_limit = 3,
    });

    store.update_model(created.metadata.id, "model-b");

    REQUIRE(
        store.load_session(created.metadata.id)->metadata.model_name ==
        "model-b");
}

TEST_CASE("sqlite store rejects a newer schema without downgrading it", "[session_store]") {
    TempDatabase database;
    sqlite3* raw_database = nullptr;
    REQUIRE(sqlite3_open(database.path().c_str(), &raw_database) == SQLITE_OK);
    REQUIRE(sqlite3_exec(
                raw_database,
                "PRAGMA user_version=2",
                nullptr,
                nullptr,
                nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_close(raw_database) == SQLITE_OK);

    REQUIRE_THROWS_AS(
        swe_agent::agent::SqliteSessionStore{database.path()},
        swe_agent::agent::SessionStorageError);

    raw_database = nullptr;
    REQUIRE(sqlite3_open(database.path().c_str(), &raw_database) == SQLITE_OK);
    sqlite3_stmt* version_query = nullptr;
    REQUIRE(sqlite3_prepare_v2(
                raw_database,
                "PRAGMA user_version",
                -1,
                &version_query,
                nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(version_query) == SQLITE_ROW);
    REQUIRE(sqlite3_column_int(version_query, 0) == 2);
    REQUIRE(sqlite3_finalize(version_query) == SQLITE_OK);
    REQUIRE(sqlite3_close(raw_database) == SQLITE_OK);
}
