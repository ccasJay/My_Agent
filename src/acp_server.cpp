#include "acp/acp_server.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
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

}  // namespace

AcpServer::AcpServer(
    JsonRpcConnection& connection,
    AcpServerContext context)
    : connection_(connection), context_(std::move(context)) {}

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
        {"agentCapabilities", Json::object()},
        {"agentInfo", {
            {"name", "my-agent"},
            {"title", "My Agent"},
            {"version", SWE_AGENT_VERSION},
        }},
        {"authMethods", Json::array()},
    });
}

void AcpServer::send_invalid_request(const Json& id, std::string message) {
    connection_.send_error(id, kInvalidRequest, std::move(message));
}

}  // namespace swe_agent::acp
