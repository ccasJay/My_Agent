#include "acp/acp_server.hpp"

#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <time.h>
#include <utility>

namespace swe_agent::acp {
namespace {

constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternalError = -32603;
constexpr int kServerNotInitialized = -32002;
constexpr std::int64_t kProtocolVersion = 1;
constexpr std::size_t kSessionPageSize = 20;

bool valid_id(const Json& id) {
    return id.is_null() || id.is_string() || id.is_number_integer() ||
        id.is_number_unsigned();
}

Json request_id_or_null(const Json& message) {
    if (!message.is_object() || !message.contains("id") ||
        !valid_id(message["id"])) {
        return nullptr;
    }
    return message["id"];
}

bool has_valid_session_setup(const Json& params) {
    return params.contains("cwd") && params["cwd"].is_string() &&
        params.contains("mcpServers") && params["mcpServers"].is_array() &&
        params["mcpServers"].empty() &&
        (!params.contains("additionalDirectories") ||
         (params["additionalDirectories"].is_array() &&
          params["additionalDirectories"].empty()));
}

bool has_visible_text(std::string_view text) {
    for (unsigned char character : text) {
        if (!std::isspace(character)) {
            return true;
        }
    }
    return false;
}

std::string strip_run_lines(std::string_view text) {
    std::istringstream input{std::string{text}};
    std::string output;
    std::string line;
    while (std::getline(input, line)) {
        std::size_t first = 0;
        while (first < line.size() &&
               (line[first] == ' ' || line[first] == '\t')) {
            ++first;
        }
        if (line.compare(first, 4, "RUN:") == 0) {
            continue;
        }
        if (!output.empty()) {
            output.push_back('\n');
        }
        output += line;
    }
    return output;
}

std::string encode_cursor(const agent::SessionListCursor& cursor) {
    return "v1:" + std::to_string(cursor.updated_at_ms) + ":" + cursor.id;
}

std::optional<agent::SessionListCursor> decode_cursor(
    std::string_view encoded) {
    if (!encoded.starts_with("v1:")) {
        return std::nullopt;
    }
    encoded.remove_prefix(3);
    const std::size_t separator = encoded.find(':');
    if (separator == std::string_view::npos || separator == 0 ||
        separator + 1 >= encoded.size()) {
        return std::nullopt;
    }
    try {
        std::size_t consumed = 0;
        const std::string timestamp_text{encoded.substr(0, separator)};
        const std::int64_t timestamp = std::stoll(timestamp_text, &consumed);
        if (consumed != timestamp_text.size()) {
            return std::nullopt;
        }
        return agent::SessionListCursor{
            .updated_at_ms = timestamp,
            .id = std::string{encoded.substr(separator + 1)},
        };
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string rfc3339_timestamp(std::int64_t milliseconds) {
    std::int64_t seconds = milliseconds / 1000;
    std::int64_t remainder = milliseconds % 1000;
    if (remainder < 0) {
        remainder += 1000;
        --seconds;
    }
    const std::time_t time = static_cast<std::time_t>(seconds);
    std::tm utc{};
    if (::gmtime_r(&time, &utc) == nullptr) {
        throw std::runtime_error{"Unable to format session timestamp"};
    }
    char date[32]{};
    if (std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &utc) == 0) {
        throw std::runtime_error{"Unable to format session timestamp"};
    }
    char timestamp[40]{};
    std::snprintf(
        timestamp,
        sizeof(timestamp),
        "%s.%03lldZ",
        date,
        static_cast<long long>(remainder));
    return timestamp;
}

}  // namespace

AcpServer::AcpServer(
    JsonRpcConnection& connection,
    AcpServerContext context)
    : connection_(connection),
      context_(std::move(context)),
      sessions_(
          context_.provider,
          context_.agent_config,
          context_.session_store,
          context_.model_name) {}

int AcpServer::run() {
    while (true) {
        const JsonRpcReadResult read_result = connection_.read();
        if (read_result.status == JsonRpcReadResult::Status::EndOfStream) {
            return 0;
        }
        if (read_result.status == JsonRpcReadResult::Status::ParseError) {
            connection_.send_error(nullptr, kParseError, "Parse error");
            continue;
        }

        try {
            dispatch(read_result.message);
        } catch (const std::exception& error) {
            std::cerr << "ACP request failed: " << error.what() << '\n';
            connection_.send_error(
                request_id_or_null(read_result.message),
                kInternalError,
                "Internal error");
        }
    }
}

void AcpServer::dispatch(const Json& message) {
    if (!message.is_object() ||
        message.value("jsonrpc", std::string{}) != "2.0") {
        send_invalid_request(request_id_or_null(message), "Invalid Request");
        return;
    }

    if (message.contains("method")) {
        if (!message["method"].is_string()) {
            send_invalid_request(request_id_or_null(message), "Invalid Request");
            return;
        }
        const Json params = message.contains("params")
            ? message["params"]
            : Json::object();
        if (!params.is_object()) {
            if (message.contains("id")) {
                connection_.send_error(
                    request_id_or_null(message),
                    kInvalidParams,
                    "Invalid params");
            }
            return;
        }

        const std::string method = message["method"].get<std::string>();
        if (message.contains("id")) {
            if (!valid_id(message["id"])) {
                send_invalid_request(nullptr, "Invalid Request");
                return;
            }
            handle_request(method, params, message["id"]);
        } else {
            handle_notification(method, params);
        }
        return;
    }

    if (message.contains("id") && valid_id(message["id"]) &&
        (message.contains("result") || message.contains("error"))) {
        handle_peer_response(message);
        return;
    }

    send_invalid_request(request_id_or_null(message), "Invalid Request");
}

void AcpServer::handle_request(
    std::string_view method,
    const Json& params,
    const Json& id) {
    if (method == "initialize") {
        handle_initialize(params, id);
        return;
    }
    if (!initialized_) {
        connection_.send_error(
            id,
            kServerNotInitialized,
            "Server not initialized");
        return;
    }
    if (method == "session/new") {
        handle_new_session(params, id);
        return;
    }
    if (method == "session/list") {
        handle_list_sessions(params, id);
        return;
    }
    if (method == "session/load") {
        handle_load_session(params, id);
        return;
    }
    if (method == "session/resume") {
        handle_resume_session(params, id);
        return;
    }
    if (method == "session/close") {
        handle_close_session(params, id);
        return;
    }
    connection_.send_error(id, kMethodNotFound, "Method not found");
}

void AcpServer::handle_notification(
    std::string_view method,
    const Json& params) {
    (void)params;
    if (!initialized_) {
        return;
    }
    std::cerr << "Ignoring unsupported ACP notification: " << method << '\n';
}

void AcpServer::handle_peer_response(const Json& message) {
    (void)message;
}

void AcpServer::handle_initialize(const Json& params, const Json& id) {
    if (initialized_) {
        connection_.send_error(id, kInvalidRequest, "Already initialized");
        return;
    }
    if (!params.contains("protocolVersion") ||
        (!params["protocolVersion"].is_number_integer() &&
         !params["protocolVersion"].is_number_unsigned()) ||
        (params.contains("clientCapabilities") &&
         !params["clientCapabilities"].is_object()) ||
        (params.contains("clientInfo") && !params["clientInfo"].is_object())) {
        connection_.send_error(id, kInvalidParams, "Invalid params");
        return;
    }

    initialized_ = true;
    connection_.send_result(id, {
        {"protocolVersion", kProtocolVersion},
        {"agentCapabilities", {
            {"loadSession", true},
            {"sessionCapabilities", {
                {"list", Json::object()},
                {"resume", Json::object()},
                {"close", Json::object()},
            }},
        }},
        {"agentInfo", {
            {"name", "my-agent"},
            {"title", "My Agent"},
            {"version", SWE_AGENT_VERSION},
        }},
        {"authMethods", Json::array()},
    });
}

void AcpServer::handle_new_session(const Json& params, const Json& id) {
    if (!has_valid_session_setup(params)) {
        connection_.send_error(id, kInvalidParams, "Invalid params");
        return;
    }
    try {
        const AcpActiveSession active = sessions_.create(
            params["cwd"].get<std::string>());
        connection_.send_result(id, {
            {"sessionId", active.session->id()},
        });
    } catch (const AcpSessionError& error) {
        connection_.send_error(id, kInvalidParams, error.what());
    }
}

void AcpServer::handle_list_sessions(const Json& params, const Json& id) {
    if ((params.contains("cwd") && !params["cwd"].is_string()) ||
        (params.contains("cursor") && !params["cursor"].is_string()) ||
        params.contains("additionalDirectories")) {
        connection_.send_error(id, kInvalidParams, "Invalid params");
        return;
    }

    try {
        agent::SessionListQuery query{.limit = kSessionPageSize};
        if (params.contains("cwd")) {
            query.workspace = AcpSessionRegistry::canonical_workspace(
                params["cwd"].get<std::string>());
        }
        if (params.contains("cursor")) {
            query.before = decode_cursor(params["cursor"].get<std::string>());
            if (!query.before.has_value()) {
                connection_.send_error(id, kInvalidParams, "Invalid cursor");
                return;
            }
        }

        const agent::SessionListPage page = sessions_.list(query);
        Json result{{"sessions", Json::array()}};
        for (const auto& session : page.sessions) {
            Json session_info{
                {"sessionId", session.id},
                {"cwd", session.workspace},
                {"updatedAt", rfc3339_timestamp(session.updated_at_ms)},
            };
            if (!session.title.empty()) {
                session_info["title"] = session.title;
            }
            result["sessions"].push_back(std::move(session_info));
        }
        if (page.next_cursor.has_value()) {
            result["nextCursor"] = encode_cursor(*page.next_cursor);
        }
        connection_.send_result(id, std::move(result));
    } catch (const AcpSessionError& error) {
        connection_.send_error(id, kInvalidParams, error.what());
    } catch (const agent::SessionStorageError& error) {
        connection_.send_error(id, kInternalError, error.what());
    }
}

void AcpServer::handle_load_session(const Json& params, const Json& id) {
    if (!has_valid_session_setup(params) ||
        !params.contains("sessionId") || !params["sessionId"].is_string()) {
        connection_.send_error(id, kInvalidParams, "Invalid params");
        return;
    }
    try {
        const AcpLoadedSession loaded = sessions_.load(
            params["sessionId"].get<std::string>(),
            params["cwd"].get<std::string>());
        replay_history(loaded.snapshot);
        connection_.send_result(id, nullptr);
    } catch (const AcpSessionError& error) {
        connection_.send_error(id, kInvalidParams, error.what());
    }
}

void AcpServer::handle_resume_session(const Json& params, const Json& id) {
    if (!has_valid_session_setup(params) ||
        !params.contains("sessionId") || !params["sessionId"].is_string()) {
        connection_.send_error(id, kInvalidParams, "Invalid params");
        return;
    }
    try {
        (void)sessions_.resume(
            params["sessionId"].get<std::string>(),
            params["cwd"].get<std::string>());
        connection_.send_result(id, Json::object());
    } catch (const AcpSessionError& error) {
        connection_.send_error(id, kInvalidParams, error.what());
    }
}

void AcpServer::handle_close_session(const Json& params, const Json& id) {
    if (!params.contains("sessionId") || !params["sessionId"].is_string()) {
        connection_.send_error(id, kInvalidParams, "Invalid params");
        return;
    }
    if (!sessions_.close(params["sessionId"].get<std::string>())) {
        connection_.send_error(id, kInvalidParams, "Session is not active");
        return;
    }
    connection_.send_result(id, Json::object());
}

void AcpServer::replay_history(const agent::SessionSnapshot& snapshot) {
    for (const auto& message : snapshot.messages) {
        std::string update_type;
        std::string content;
        if (message.kind == agent::SessionMessageKind::UserPrompt) {
            update_type = "user_message_chunk";
            content = message.content;
        } else if (message.kind == agent::SessionMessageKind::Assistant) {
            update_type = "agent_message_chunk";
            content = strip_run_lines(message.content);
        } else {
            continue;
        }
        if (!has_visible_text(content)) {
            continue;
        }
        connection_.send_notification("session/update", {
            {"sessionId", snapshot.metadata.id},
            {"update", {
                {"sessionUpdate", update_type},
                {"content", {
                    {"type", "text"},
                    {"text", std::move(content)},
                }},
            }},
        });
    }
}

void AcpServer::send_invalid_request(const Json& id, std::string message) {
    connection_.send_error(id, kInvalidRequest, std::move(message));
}

}  // namespace swe_agent::acp
