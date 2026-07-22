#include "acp/json_rpc.hpp"

#include <stdexcept>
#include <utility>

namespace swe_agent::acp {

JsonRpcConnection::JsonRpcConnection(
    std::istream& input,
    std::ostream& output)
    : input_(input), output_(output) {}

JsonRpcReadResult JsonRpcConnection::read() {
    std::string line;
    if (!std::getline(input_, line)) {
        return {.status = JsonRpcReadResult::Status::EndOfStream};
    }

    try {
        return {
            .status = JsonRpcReadResult::Status::Message,
            .message = Json::parse(line),
        };
    } catch (const Json::parse_error&) {
        return {.status = JsonRpcReadResult::Status::ParseError};
    }
}

void JsonRpcConnection::send_result(const Json& id, Json result) {
    write({
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", std::move(result)},
    });
}

void JsonRpcConnection::send_error(
    const Json& id,
    int code,
    std::string message,
    std::optional<Json> data) {
    Json error{
        {"code", code},
        {"message", std::move(message)},
    };
    if (data.has_value()) {
        error["data"] = std::move(*data);
    }
    write({
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", std::move(error)},
    });
}

void JsonRpcConnection::send_notification(
    std::string_view method,
    Json params) {
    write({
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", std::move(params)},
    });
}

Json JsonRpcConnection::send_request(
    std::string_view method,
    Json params) {
    const Json id = "agent-" +
        std::to_string(next_request_id_.fetch_add(1));
    write({
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method},
        {"params", std::move(params)},
    });
    return id;
}

void JsonRpcConnection::write(Json message) {
    std::lock_guard lock{write_mutex_};
    output_ << message.dump() << '\n';
    output_.flush();
    if (!output_) {
        throw std::runtime_error{"Unable to write JSON-RPC message"};
    }
}

}  // namespace swe_agent::acp
