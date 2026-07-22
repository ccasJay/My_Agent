#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <istream>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace swe_agent::acp {

using Json = nlohmann::json;

/** @brief JSON-RPC 读取结果。 */
struct JsonRpcReadResult {
    enum class Status {
        Message,
        ParseError,
        EndOfStream,
    };

    Status status{Status::EndOfStream};
    Json message;
};

/**
 * @brief 基于换行分帧的线程安全 JSON-RPC 连接。
 *
 * 读取由协议线程串行调用；写入可来自协议线程或 Prompt Worker。
 */
class JsonRpcConnection {
public:
    JsonRpcConnection(std::istream& input, std::ostream& output);

    /** @brief 读取并解析下一条物理行。 */
    [[nodiscard]] JsonRpcReadResult read();

    /** @brief 发送成功响应。 */
    void send_result(const Json& id, Json result);

    /** @brief 发送 JSON-RPC 错误响应。 */
    void send_error(
        const Json& id,
        int code,
        std::string message,
        std::optional<Json> data = std::nullopt);

    /** @brief 发送单向通知。 */
    void send_notification(std::string_view method, Json params);

    /**
     * @brief 发送由 Agent 发起的反向请求。
     * @return 本次请求使用的字符串 ID。
     */
    [[nodiscard]] Json send_request(std::string_view method, Json params);

private:
    void write(Json message);

    std::istream& input_;
    std::ostream& output_;
    std::mutex write_mutex_;
    std::atomic<std::uint64_t> next_request_id_{1};
};

}  // namespace swe_agent::acp
