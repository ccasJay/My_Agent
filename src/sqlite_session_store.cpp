#include "agent/sqlite_session_store.hpp"

#include <sqlite3.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <utility>

namespace swe_agent::agent {
namespace {

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string session_title(std::string_view content) {
    constexpr std::size_t kMaxTitleBytes = 80;
    std::string title{content.substr(0, content.find('\n'))};
    if (title.size() <= kMaxTitleBytes) {
        return title;
    }

    std::size_t boundary = kMaxTitleBytes;
    while (boundary > 0 &&
           (static_cast<unsigned char>(title[boundary]) & 0xC0U) == 0x80U) {
        --boundary;
    }
    title.resize(boundary);
    return title;
}

std::string sqlite_error(sqlite3* database, std::string_view operation) {
    return std::string{operation} + ": " + sqlite3_errmsg(database);
}

void check(sqlite3* database, int result, std::string_view operation) {
    if (result != SQLITE_OK && result != SQLITE_DONE && result != SQLITE_ROW) {
        throw SessionStorageError{sqlite_error(database, operation)};
    }
}

void execute(sqlite3* database, const char* sql) {
    char* error = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &error);
    if (result == SQLITE_OK) {
        return;
    }
    const std::string message = error != nullptr
        ? std::string{error}
        : sqlite_error(database, "execute SQL");
    sqlite3_free(error);
    throw SessionStorageError{message};
}

class Statement {
public:
    Statement(sqlite3* database, std::string_view sql) : database_(database) {
        check(
            database_,
            sqlite3_prepare_v2(
                database_,
                sql.data(),
                static_cast<int>(sql.size()),
                &statement_,
                nullptr),
            "prepare SQL");
    }

    ~Statement() {
        sqlite3_finalize(statement_);
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind(int index, std::string_view value) {
        check(
            database_,
            sqlite3_bind_text(
                statement_,
                index,
                value.data(),
                static_cast<int>(value.size()),
                SQLITE_TRANSIENT),
            "bind text");
    }

    void bind(int index, std::int64_t value) {
        check(
            database_,
            sqlite3_bind_int64(statement_, index, value),
            "bind integer");
    }

    int step() {
        const int result = sqlite3_step(statement_);
        check(database_, result, "step SQL");
        return result;
    }

    [[nodiscard]] std::string text(int column) const {
        const auto* value = sqlite3_column_text(statement_, column);
        const int size = sqlite3_column_bytes(statement_, column);
        return value == nullptr
            ? std::string{}
            : std::string{
                  reinterpret_cast<const char*>(value),
                  static_cast<std::size_t>(size)};
    }

    [[nodiscard]] std::int64_t integer(int column) const {
        return sqlite3_column_int64(statement_, column);
    }

private:
    sqlite3* database_;
    sqlite3_stmt* statement_{nullptr};
};

std::string role_name(model::Role role) {
    switch (role) {
    case model::Role::System:
        return "system";
    case model::Role::User:
        return "user";
    case model::Role::Assistant:
        return "assistant";
    case model::Role::Tool:
        return "tool";
    }
    throw SessionStorageError{"Unknown message role"};
}

model::Role parse_role(std::string_view role) {
    if (role == "system") {
        return model::Role::System;
    }
    if (role == "user") {
        return model::Role::User;
    }
    if (role == "assistant") {
        return model::Role::Assistant;
    }
    if (role == "tool") {
        return model::Role::Tool;
    }
    throw SessionStorageError{"Invalid message role in database"};
}

std::string kind_name(SessionMessageKind kind) {
    switch (kind) {
    case SessionMessageKind::System:
        return "system";
    case SessionMessageKind::UserPrompt:
        return "user_prompt";
    case SessionMessageKind::Assistant:
        return "assistant";
    case SessionMessageKind::Observation:
        return "observation";
    case SessionMessageKind::HostHint:
        return "host_hint";
    }
    throw SessionStorageError{"Unknown session message kind"};
}

SessionMessageKind parse_kind(std::string_view kind) {
    if (kind == "system") {
        return SessionMessageKind::System;
    }
    if (kind == "user_prompt") {
        return SessionMessageKind::UserPrompt;
    }
    if (kind == "assistant") {
        return SessionMessageKind::Assistant;
    }
    if (kind == "observation") {
        return SessionMessageKind::Observation;
    }
    if (kind == "host_hint") {
        return SessionMessageKind::HostHint;
    }
    throw SessionStorageError{"Invalid message kind in database"};
}

class Transaction {
public:
    explicit Transaction(sqlite3* database) : database_(database) {
        execute(database_, "BEGIN IMMEDIATE");
    }

    ~Transaction() {
        if (!committed_) {
            sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }

    void commit() {
        execute(database_, "COMMIT");
        committed_ = true;
    }

private:
    sqlite3* database_;
    bool committed_{false};
};

}  // namespace

struct SqliteSessionStore::Impl {
    Impl(
        const std::filesystem::path& database_path,
        SessionStoreLimits store_limits)
        : path(database_path), limits(store_limits) {
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
            chmod(path.parent_path().c_str(), S_IRWXU);
        }

        sqlite3* opened = nullptr;
        const int result = sqlite3_open_v2(
            path.c_str(),
            &opened,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
            nullptr);
        database = opened;
        if (result != SQLITE_OK) {
            const std::string message = database != nullptr
                ? sqlite_error(database, "open session database")
                : "open session database failed";
            if (database != nullptr) {
                sqlite3_close(database);
                database = nullptr;
            }
            throw SessionStorageError{message};
        }

        chmod(path.c_str(), S_IRUSR | S_IWUSR);
        execute(database, "PRAGMA foreign_keys=ON");
        execute(database, "PRAGMA journal_mode=WAL");
        execute(database, "PRAGMA synchronous=NORMAL");
        execute(database, "PRAGMA busy_timeout=3000");

        Statement version_query{database, "PRAGMA user_version"};
        if (version_query.step() != SQLITE_ROW) {
            throw SessionStorageError{"Unable to read session schema version"};
        }
        const std::int64_t schema_version = version_query.integer(0);
        if (schema_version > 1) {
            throw SessionStorageError{
                "Session database schema is newer than this application"};
        }
        if (schema_version == 0) {
            Transaction migration{database};
            execute(
                database,
                "CREATE TABLE IF NOT EXISTS sessions ("
                "id TEXT PRIMARY KEY,"
                "title TEXT NOT NULL DEFAULT '',"
                "workspace TEXT NOT NULL,"
                "model_name TEXT NOT NULL,"
                "system_prompt TEXT NOT NULL,"
                "step_limit INTEGER NOT NULL,"
                "created_at_ms INTEGER NOT NULL,"
                "updated_at_ms INTEGER NOT NULL,"
                "archived_at_ms INTEGER NULL"
                ")");
            execute(
                database,
                "CREATE TABLE IF NOT EXISTS messages ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,"
                "sequence INTEGER NOT NULL,"
                "role TEXT NOT NULL,"
                "kind TEXT NOT NULL,"
                "content TEXT NOT NULL,"
                "created_at_ms INTEGER NOT NULL,"
                "UNIQUE(session_id, sequence)"
                ")");
            execute(
                database,
                "CREATE INDEX IF NOT EXISTS idx_sessions_workspace_updated "
                "ON sessions(workspace, updated_at_ms DESC)");
            execute(
                database,
                "CREATE INDEX IF NOT EXISTS idx_messages_session_sequence "
                "ON messages(session_id, sequence)");
            execute(database, "PRAGMA user_version=1");
            migration.commit();
        }
    }

    ~Impl() {
        if (database != nullptr) {
            sqlite3_close(database);
        }
    }

    std::filesystem::path path;
    SessionStoreLimits limits;
    sqlite3* database{nullptr};
};

SqliteSessionStore::SqliteSessionStore(
    std::filesystem::path database_path,
    SessionStoreLimits limits)
    : impl_(std::make_unique<Impl>(database_path, limits)) {}

SqliteSessionStore::~SqliteSessionStore() = default;
SqliteSessionStore::SqliteSessionStore(SqliteSessionStore&&) noexcept = default;
SqliteSessionStore& SqliteSessionStore::operator=(
    SqliteSessionStore&&) noexcept = default;

SessionSnapshot SqliteSessionStore::create_session(const SessionSeed& seed) {
    if (seed.system_prompt.size() > impl_->limits.max_message_bytes ||
        seed.system_prompt.size() > impl_->limits.max_session_bytes) {
        throw SessionStorageError{"System message exceeds session limits"};
    }

    Transaction transaction{impl_->database};
    Statement id_query{impl_->database, "SELECT lower(hex(randomblob(16)))"};
    if (id_query.step() != SQLITE_ROW) {
        throw SessionStorageError{"Unable to generate session id"};
    }

    const SessionId id = id_query.text(0);
    const std::int64_t timestamp = now_ms();
    Statement insert_session{
        impl_->database,
        "INSERT INTO sessions("
        "id, title, workspace, model_name, system_prompt, step_limit, "
        "created_at_ms, updated_at_ms) VALUES(?, '', ?, ?, ?, ?, ?, ?)"};
    insert_session.bind(1, id);
    insert_session.bind(2, seed.workspace);
    insert_session.bind(3, seed.model_name);
    insert_session.bind(4, seed.system_prompt);
    insert_session.bind(5, static_cast<std::int64_t>(seed.step_limit));
    insert_session.bind(6, timestamp);
    insert_session.bind(7, timestamp);
    insert_session.step();

    Statement insert_message{
        impl_->database,
        "INSERT INTO messages("
        "session_id, sequence, role, kind, content, created_at_ms) "
        "VALUES(?, 0, ?, ?, ?, ?)"};
    insert_message.bind(1, id);
    insert_message.bind(2, role_name(model::Role::System));
    insert_message.bind(3, kind_name(SessionMessageKind::System));
    insert_message.bind(4, seed.system_prompt);
    insert_message.bind(5, timestamp);
    insert_message.step();

    transaction.commit();
    return *load_session(id);
}

void SqliteSessionStore::append_message(
    std::string_view id,
    const SessionMessage& message) {
    if (message.content.size() > impl_->limits.max_message_bytes) {
        throw SessionStorageError{"Session message exceeds size limit"};
    }

    Transaction transaction{impl_->database};
    if (message.sequence >
        static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        throw SessionStorageError{"Session message sequence is out of range"};
    }
    Statement sequence_query{
        impl_->database,
        "SELECT COALESCE(MAX(sequence), -1) + 1 FROM messages "
        "WHERE session_id = ?"};
    sequence_query.bind(1, id);
    if (sequence_query.step() != SQLITE_ROW ||
        sequence_query.integer(0) !=
            static_cast<std::int64_t>(message.sequence)) {
        throw SessionStorageError{"Session message sequence is not contiguous"};
    }

    Statement size_query{
        impl_->database,
        "SELECT COALESCE(SUM(length(CAST(content AS BLOB))), 0) "
        "FROM messages WHERE session_id = ?"};
    size_query.bind(1, id);
    if (size_query.step() != SQLITE_ROW) {
        throw SessionStorageError{"Unable to calculate session size"};
    }
    const auto current_size = static_cast<std::uint64_t>(size_query.integer(0));
    if (current_size + message.content.size() >
        impl_->limits.max_session_bytes) {
        throw SessionStorageError{"Session exceeds total size limit"};
    }

    Statement insert_message{
        impl_->database,
        "INSERT INTO messages("
        "session_id, sequence, role, kind, content, created_at_ms) "
        "VALUES(?, ?, ?, ?, ?, ?)"};
    insert_message.bind(1, id);
    insert_message.bind(2, static_cast<std::int64_t>(message.sequence));
    insert_message.bind(3, role_name(message.role));
    insert_message.bind(4, kind_name(message.kind));
    insert_message.bind(5, message.content);
    insert_message.bind(6, message.created_at_ms);
    insert_message.step();

    const std::int64_t timestamp = now_ms();
    Statement update_session{
        impl_->database,
        "UPDATE sessions SET updated_at_ms = ?, title = CASE "
        "WHEN title = '' AND ? = 'user_prompt' THEN ? ELSE title END "
        "WHERE id = ? AND archived_at_ms IS NULL"};
    const std::string title = session_title(message.content);
    update_session.bind(1, timestamp);
    update_session.bind(2, kind_name(message.kind));
    update_session.bind(3, title);
    update_session.bind(4, id);
    update_session.step();
    if (sqlite3_changes(impl_->database) != 1) {
        throw SessionStorageError{"Session not found or archived"};
    }
    transaction.commit();
}

std::optional<SessionSnapshot> SqliteSessionStore::load_session(
    std::string_view id) {
    Statement session_query{
        impl_->database,
        "SELECT id, title, workspace, model_name, system_prompt, step_limit, "
        "created_at_ms, updated_at_ms FROM sessions "
        "WHERE id = ? AND archived_at_ms IS NULL"};
    session_query.bind(1, id);
    if (session_query.step() != SQLITE_ROW) {
        return std::nullopt;
    }

    SessionSnapshot snapshot{
        .metadata = {
            .id = session_query.text(0),
            .title = session_query.text(1),
            .workspace = session_query.text(2),
            .model_name = session_query.text(3),
            .system_prompt = session_query.text(4),
            .step_limit = static_cast<std::size_t>(session_query.integer(5)),
            .created_at_ms = session_query.integer(6),
            .updated_at_ms = session_query.integer(7),
        },
    };

    Statement message_query{
        impl_->database,
        "SELECT sequence, role, kind, content, created_at_ms FROM messages "
        "WHERE session_id = ? ORDER BY sequence"};
    message_query.bind(1, id);
    while (message_query.step() == SQLITE_ROW) {
        snapshot.messages.push_back({
            .sequence = static_cast<std::size_t>(message_query.integer(0)),
            .role = parse_role(message_query.text(1)),
            .kind = parse_kind(message_query.text(2)),
            .content = message_query.text(3),
            .created_at_ms = message_query.integer(4),
        });
    }
    return snapshot;
}

std::optional<SessionSnapshot> SqliteSessionStore::latest_session(
    std::string_view workspace) {
    Statement query{
        impl_->database,
        "SELECT id FROM sessions "
        "WHERE workspace = ? AND archived_at_ms IS NULL "
        "ORDER BY updated_at_ms DESC, id DESC LIMIT 1"};
    query.bind(1, workspace);
    if (query.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return load_session(query.text(0));
}

SessionListPage SqliteSessionStore::list_sessions_page(
    const SessionListQuery& request) {
    if (request.limit == 0 ||
        request.limit > static_cast<std::size_t>(
            std::numeric_limits<std::int64_t>::max())) {
        throw SessionStorageError{"Session list limit is out of range"};
    }
    if (request.before.has_value() && request.before->id.empty()) {
        throw SessionStorageError{"Session list cursor has an empty id"};
    }

    std::string sql =
        "SELECT id, title, workspace, model_name, updated_at_ms "
        "FROM sessions WHERE archived_at_ms IS NULL ";
    if (request.workspace.has_value()) {
        sql += "AND workspace = ? ";
    }
    if (request.before.has_value()) {
        sql +=
            "AND (updated_at_ms < ? OR (updated_at_ms = ? AND id < ?)) ";
    }
    sql += "ORDER BY updated_at_ms DESC, id DESC LIMIT ?";

    Statement query{impl_->database, sql};
    int bind_index = 1;
    if (request.workspace.has_value()) {
        query.bind(bind_index++, *request.workspace);
    }
    if (request.before.has_value()) {
        query.bind(bind_index++, request.before->updated_at_ms);
        query.bind(bind_index++, request.before->updated_at_ms);
        query.bind(bind_index++, request.before->id);
    }
    const bool can_detect_more = request.limit < static_cast<std::size_t>(
        std::numeric_limits<std::int64_t>::max());
    const std::size_t fetch_limit = request.limit + (can_detect_more ? 1 : 0);
    query.bind(bind_index, static_cast<std::int64_t>(fetch_limit));

    SessionListPage page;
    while (query.step() == SQLITE_ROW) {
        page.sessions.push_back({
            .id = query.text(0),
            .title = query.text(1),
            .workspace = query.text(2),
            .model_name = query.text(3),
            .updated_at_ms = query.integer(4),
        });
    }
    if (page.sessions.size() > request.limit) {
        page.sessions.resize(request.limit);
        const SessionSummary& last = page.sessions.back();
        page.next_cursor = SessionListCursor{
            .updated_at_ms = last.updated_at_ms,
            .id = last.id,
        };
    }
    return page;
}

void SqliteSessionStore::reset_session(
    std::string_view id,
    std::string_view system_prompt) {
    if (system_prompt.size() > impl_->limits.max_message_bytes ||
        system_prompt.size() > impl_->limits.max_session_bytes) {
        throw SessionStorageError{"System message exceeds session limits"};
    }

    Transaction transaction{impl_->database};
    const std::int64_t timestamp = now_ms();
    Statement update_session{
        impl_->database,
        "UPDATE sessions SET title = '', system_prompt = ?, updated_at_ms = ? "
        "WHERE id = ? AND archived_at_ms IS NULL"};
    update_session.bind(1, system_prompt);
    update_session.bind(2, timestamp);
    update_session.bind(3, id);
    update_session.step();
    if (sqlite3_changes(impl_->database) != 1) {
        throw SessionStorageError{"Session not found or archived"};
    }

    Statement delete_messages{
        impl_->database,
        "DELETE FROM messages WHERE session_id = ?"};
    delete_messages.bind(1, id);
    delete_messages.step();

    Statement insert_message{
        impl_->database,
        "INSERT INTO messages("
        "session_id, sequence, role, kind, content, created_at_ms) "
        "VALUES(?, 0, 'system', 'system', ?, ?)"};
    insert_message.bind(1, id);
    insert_message.bind(2, system_prompt);
    insert_message.bind(3, timestamp);
    insert_message.step();
    transaction.commit();
}

void SqliteSessionStore::update_model(
    std::string_view id,
    std::string_view model_name) {
    Statement statement{
        impl_->database,
        "UPDATE sessions SET model_name = ?, updated_at_ms = ? "
        "WHERE id = ? AND archived_at_ms IS NULL"};
    statement.bind(1, model_name);
    statement.bind(2, now_ms());
    statement.bind(3, id);
    statement.step();
    if (sqlite3_changes(impl_->database) != 1) {
        throw SessionStorageError{"Session not found or archived"};
    }
}

void SqliteSessionStore::archive_session(std::string_view id) {
    const std::int64_t timestamp = now_ms();
    Statement statement{
        impl_->database,
        "UPDATE sessions SET archived_at_ms = ?, updated_at_ms = ? "
        "WHERE id = ? AND archived_at_ms IS NULL"};
    statement.bind(1, timestamp);
    statement.bind(2, timestamp);
    statement.bind(3, id);
    statement.step();
    if (sqlite3_changes(impl_->database) != 1) {
        throw SessionStorageError{"Session not found or already archived"};
    }
}

}  // namespace swe_agent::agent
