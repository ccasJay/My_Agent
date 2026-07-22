#include <catch2/catch_test_macros.hpp>

#include "acp/acp_server.hpp"
#include "acp/json_rpc.hpp"
#include "agent/sqlite_session_store.hpp"
#include "config/agent_loader.hpp"
#include "model/model.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <functional>
#include <istream>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Json = swe_agent::acp::Json;

class TempWorkspace {
public:
    TempWorkspace()
        : path_(
              std::filesystem::temp_directory_path() /
              ("swe-agent-acp-" + std::to_string(
                  std::chrono::steady_clock::now()
                      .time_since_epoch()
                      .count()))) {
        std::filesystem::create_directories(path_);
    }

    ~TempWorkspace() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class QueueInputBuffer final : public std::streambuf {
public:
    void push(std::string line) {
        line.push_back('\n');
        {
            std::lock_guard lock{mutex_};
            for (char character : line) {
                data_.push_back(character);
            }
        }
        condition_.notify_all();
    }

    void close() {
        {
            std::lock_guard lock{mutex_};
            closed_ = true;
        }
        condition_.notify_all();
    }

protected:
    int_type underflow() override {
        std::unique_lock lock{mutex_};
        condition_.wait(lock, [&] { return closed_ || !data_.empty(); });
        if (data_.empty()) {
            return traits_type::eof();
        }
        return traits_type::to_int_type(data_.front());
    }

    int_type uflow() override {
        std::unique_lock lock{mutex_};
        condition_.wait(lock, [&] { return closed_ || !data_.empty(); });
        if (data_.empty()) {
            return traits_type::eof();
        }
        const char character = data_.front();
        data_.pop_front();
        return traits_type::to_int_type(character);
    }

    std::streamsize xsgetn(char* output, std::streamsize count) override {
        std::streamsize copied = 0;
        while (copied < count) {
            const int_type character = uflow();
            if (traits_type::eq_int_type(character, traits_type::eof())) {
                break;
            }
            output[copied++] = traits_type::to_char_type(character);
        }
        return copied;
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<char> data_;
    bool closed_{false};
};

class QueueInputStream final : public std::istream {
public:
    QueueInputStream() : std::istream(&buffer_) {}

    void push(const Json& message) {
        buffer_.push(message.dump());
    }

    void close() {
        buffer_.close();
    }

private:
    QueueInputBuffer buffer_;
};

class LockedOutputBuffer final : public std::streambuf {
public:
    [[nodiscard]] std::string snapshot() const {
        std::lock_guard lock{mutex_};
        return data_;
    }

protected:
    std::streamsize xsputn(
        const char* input,
        std::streamsize count) override {
        std::lock_guard lock{mutex_};
        data_.append(input, static_cast<std::size_t>(count));
        return count;
    }

    int_type overflow(int_type character) override {
        if (traits_type::eq_int_type(character, traits_type::eof())) {
            return traits_type::not_eof(character);
        }
        std::lock_guard lock{mutex_};
        data_.push_back(traits_type::to_char_type(character));
        return character;
    }

private:
    mutable std::mutex mutex_;
    std::string data_;
};

class LockedOutputStream final : public std::ostream {
public:
    LockedOutputStream() : std::ostream(&buffer_) {}

    [[nodiscard]] std::string snapshot() const {
        return buffer_.snapshot();
    }

private:
    LockedOutputBuffer buffer_;
};

class FakeProvider final : public swe_agent::model::IProvider {
public:
    void set_responses(std::vector<std::string> responses) {
        std::lock_guard lock{mutex_};
        responses_ = std::move(responses);
    }

    void block_queries() {
        std::lock_guard lock{mutex_};
        block_queries_ = true;
        released_ = false;
    }

    void wait_until_queried() {
        std::unique_lock lock{mutex_};
        condition_.wait_for(lock, std::chrono::seconds{2}, [&] {
            return query_entered_;
        });
        if (!query_entered_) {
            throw std::runtime_error{"Timed out waiting for fake provider"};
        }
    }

    void release() {
        {
            std::lock_guard lock{mutex_};
            released_ = true;
        }
        condition_.notify_all();
    }

    swe_agent::model::ModelResponse query(
        const swe_agent::model::MSG&) override {
        std::unique_lock lock{mutex_};
        query_entered_ = true;
        condition_.notify_all();
        if (block_queries_) {
            condition_.wait(lock, [&] { return released_; });
        }
        if (responses_.empty()) {
            return {};
        }
        std::string response = std::move(responses_.front());
        responses_.erase(responses_.begin());
        return {std::move(response)};
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<std::string> responses_;
    bool block_queries_{false};
    bool query_entered_{false};
    bool released_{false};
};

std::vector<Json> parse_complete_messages(std::string_view output) {
    std::vector<Json> messages;
    std::size_t start = 0;
    while (start < output.size()) {
        const std::size_t newline = output.find('\n', start);
        if (newline == std::string_view::npos) {
            break;
        }
        const std::string_view line = output.substr(start, newline - start);
        if (!line.empty()) {
            messages.push_back(Json::parse(line));
        }
        start = newline + 1;
    }
    return messages;
}

class ProtocolHarness {
public:
    ProtocolHarness(
        const std::filesystem::path& workspace,
        FakeProvider& provider,
        std::size_t active_session_limit = 64)
        : workspace_(workspace.string()),
          store_(workspace / "agent.db"),
          connection_(input_, output_),
          server_(
              connection_,
              {
                  .provider = provider,
                  .agent_config = {
                      .system_prompt = "system",
                      .user_prompt = "unused",
                      .step_limit = 5,
                  },
                  .session_store = store_,
                  .model_name = "fake-model",
                  .active_session_limit = active_session_limit,
              }) {}

    ~ProtocolHarness() {
        stop();
    }

    void start() {
        thread_ = std::thread([this] { (void)server_.run(); });
    }

    void send(Json message) {
        input_.push(message);
    }

    void stop() {
        input_.close();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    [[nodiscard]] Json wait_for(
        const std::function<bool(const Json&)>& predicate) const {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (std::chrono::steady_clock::now() < deadline) {
            for (const auto& message :
                 parse_complete_messages(output_.snapshot())) {
                if (predicate(message)) {
                    return message;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        throw std::runtime_error{"Timed out waiting for ACP message"};
    }

    [[nodiscard]] std::vector<Json> messages() const {
        return parse_complete_messages(output_.snapshot());
    }

    [[nodiscard]] const std::string& workspace() const noexcept {
        return workspace_;
    }

private:
    std::string workspace_;
    swe_agent::agent::SqliteSessionStore store_;
    QueueInputStream input_;
    LockedOutputStream output_;
    swe_agent::acp::JsonRpcConnection connection_;
    swe_agent::acp::AcpServer server_;
    std::thread thread_;
};

bool is_response(const Json& message, int id) {
    return message.is_object() && message.contains("id") &&
        message["id"] == id;
}

bool is_method(const Json& message, std::string_view method) {
    return message.is_object() && message.contains("method") &&
        message["method"] == method;
}

std::string initialize_and_create(ProtocolHarness& harness) {
    harness.send({
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {{"protocolVersion", 1}}},
    });
    const Json initialized = harness.wait_for([](const Json& message) {
        return is_response(message, 1);
    });
    REQUIRE(initialized["result"]["protocolVersion"] == 1);

    harness.send({
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "session/new"},
        {"params", {
            {"cwd", harness.workspace()},
            {"mcpServers", Json::array()},
        }},
    });
    const Json created = harness.wait_for([](const Json& message) {
        return is_response(message, 2);
    });
    return created["result"]["sessionId"].get<std::string>();
}

}  // namespace

TEST_CASE("ACP server runs a complete prompt and restores it", "[acp][server]") {
    TempWorkspace workspace;
    std::string session_id;
    {
        FakeProvider provider;
        provider.set_responses({
            "Working\nRUN: echo hello\n",
            "Conclusion: done\nRUN: echo COMPLETE_TASK\n",
        });
        ProtocolHarness harness{workspace.path(), provider};
        harness.start();
        session_id = initialize_and_create(harness);
        harness.send({
            {"jsonrpc", "2.0"},
            {"id", 3},
            {"method", "session/prompt"},
            {"params", {
                {"sessionId", session_id},
                {"prompt", Json::array({
                    {{"type", "text"}, {"text", "say hello"}},
                    {
                        {"type", "resource_link"},
                        {"uri", "file:///tmp/example.cpp"},
                        {"name", "example.cpp"},
                    },
                })},
            }},
        });

        const Json completed = harness.wait_for([](const Json& message) {
            return is_response(message, 3);
        });
        REQUIRE(completed["result"]["stopReason"] == "end_turn");

        const auto messages = harness.messages();
        REQUIRE(std::any_of(messages.begin(), messages.end(), [](const Json& message) {
            return is_method(message, "session/update") &&
                message["params"]["update"].value("sessionUpdate", "") ==
                    "tool_call";
        }));
        REQUIRE(std::any_of(messages.begin(), messages.end(), [](const Json& message) {
            return is_method(message, "session/update") &&
                message["params"]["update"].value("status", "") ==
                    "completed";
        }));
        harness.stop();
    }

    FakeProvider restored_provider;
    ProtocolHarness restored{workspace.path(), restored_provider};
    restored.start();
    restored.send({
        {"jsonrpc", "2.0"},
        {"id", 10},
        {"method", "initialize"},
        {"params", {{"protocolVersion", 1}}},
    });
    (void)restored.wait_for([](const Json& message) {
        return is_response(message, 10);
    });
    restored.send({
        {"jsonrpc", "2.0"},
        {"id", 11},
        {"method", "session/list"},
        {"params", {{"cwd", restored.workspace()}}},
    });
    const Json listed = restored.wait_for([](const Json& message) {
        return is_response(message, 11);
    });
    REQUIRE(listed["result"]["sessions"][0]["sessionId"] == session_id);

    restored.send({
        {"jsonrpc", "2.0"},
        {"id", 12},
        {"method", "session/load"},
        {"params", {
            {"sessionId", session_id},
            {"cwd", restored.workspace()},
            {"mcpServers", Json::array()},
        }},
    });
    const Json loaded = restored.wait_for([](const Json& message) {
        return is_response(message, 12);
    });
    REQUIRE(loaded["result"].is_object());
    const auto restored_messages = restored.messages();
    REQUIRE(std::any_of(
        restored_messages.begin(),
        restored_messages.end(),
        [](const Json& message) {
            return is_method(message, "session/update") &&
                message["params"]["update"].value("sessionUpdate", "") ==
                    "user_message_chunk";
        }));

    restored.send({
        {"jsonrpc", "2.0"},
        {"id", 13},
        {"method", "session/close"},
        {"params", {{"sessionId", session_id}}},
    });
    (void)restored.wait_for([](const Json& message) {
        return is_response(message, 13);
    });
    restored.send({
        {"jsonrpc", "2.0"},
        {"id", 14},
        {"method", "session/resume"},
        {"params", {
            {"sessionId", session_id},
            {"cwd", restored.workspace()},
        }},
    });
    const Json resumed = restored.wait_for([](const Json& message) {
        return is_response(message, 14);
    });
    REQUIRE(resumed["result"].is_object());
    restored.stop();
}

TEST_CASE("ACP server resolves command permission through the client", "[acp][permission]") {
    TempWorkspace workspace;
    FakeProvider provider;
    provider.set_responses({
        "Review this\nRUN: sh -c 'echo reviewed'\n",
        "Conclusion: reviewed\nRUN: echo COMPLETE_TASK\n",
    });
    ProtocolHarness harness{workspace.path(), provider};
    harness.start();
    const std::string session_id = initialize_and_create(harness);
    harness.send({
        {"jsonrpc", "2.0"},
        {"id", 3},
        {"method", "session/prompt"},
        {"params", {
            {"sessionId", session_id},
            {"prompt", Json::array({{{"type", "text"}, {"text", "review"}}})},
        }},
    });

    const Json permission = harness.wait_for([](const Json& message) {
        return is_method(message, "session/request_permission");
    });
    REQUIRE(permission["params"]["options"].size() == 2);
    harness.send({
        {"jsonrpc", "2.0"},
        {"id", permission["id"]},
        {"result", {
            {"outcome", {
                {"outcome", "selected"},
                {"optionId", "allow-once"},
            }},
        }},
    });

    const Json completed = harness.wait_for([](const Json& message) {
        return is_response(message, 3);
    });
    REQUIRE(completed["result"]["stopReason"] == "end_turn");
    harness.stop();
}

TEST_CASE("ACP server rejects concurrent prompts and honours cancellation", "[acp][cancel]") {
    TempWorkspace workspace;
    FakeProvider provider;
    provider.set_responses({
        "Conclusion: too late\nRUN: echo COMPLETE_TASK\n",
    });
    provider.block_queries();
    ProtocolHarness harness{workspace.path(), provider};
    harness.start();
    const std::string session_id = initialize_and_create(harness);
    const Json prompt_params{
        {"sessionId", session_id},
        {"prompt", Json::array({{{"type", "text"}, {"text", "wait"}}})},
    };
    harness.send({
        {"jsonrpc", "2.0"},
        {"id", 3},
        {"method", "session/prompt"},
        {"params", prompt_params},
    });
    provider.wait_until_queried();
    harness.send({
        {"jsonrpc", "2.0"},
        {"id", 4},
        {"method", "session/prompt"},
        {"params", prompt_params},
    });
    const Json busy = harness.wait_for([](const Json& message) {
        return is_response(message, 4);
    });
    REQUIRE(busy["error"]["code"] == -32000);

    harness.send({
        {"jsonrpc", "2.0"},
        {"method", "session/cancel"},
        {"params", {{"sessionId", session_id}}},
    });
    provider.release();
    const Json cancelled = harness.wait_for([](const Json& message) {
        return is_response(message, 3);
    });
    REQUIRE(cancelled["result"]["stopReason"] == "cancelled");
    harness.stop();
}

TEST_CASE("ACP server rejects unsupported MCP servers", "[acp][validation]") {
    TempWorkspace workspace;
    FakeProvider provider;
    ProtocolHarness harness{workspace.path(), provider};
    harness.start();
    harness.send({
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {{"protocolVersion", 1}}},
    });
    (void)harness.wait_for([](const Json& message) {
        return is_response(message, 1);
    });
    harness.send({
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "session/new"},
        {"params", {
            {"cwd", harness.workspace()},
            {"mcpServers", Json::array({
                {
                    {"name", "unsupported"},
                    {"command", "server"},
                    {"args", Json::array()},
                    {"env", Json::array()},
                },
            })},
        }},
    });
    const Json rejected = harness.wait_for([](const Json& message) {
        return is_response(message, 2);
    });
    REQUIRE(rejected["error"]["code"] == -32602);
    harness.stop();
}

TEST_CASE("ACP server validates protocol version range", "[acp][validation]") {
    TempWorkspace workspace;
    FakeProvider provider;
    ProtocolHarness harness{workspace.path(), provider};
    harness.start();

    harness.send({
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {{"protocolVersion", -1}}},
    });
    const Json negative = harness.wait_for([](const Json& message) {
        return is_response(message, 1);
    });
    REQUIRE(negative["error"]["code"] == -32602);

    harness.send({
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "initialize"},
        {"params", {{"protocolVersion", 65536}}},
    });
    const Json too_large = harness.wait_for([](const Json& message) {
        return is_response(message, 2);
    });
    REQUIRE(too_large["error"]["code"] == -32602);

    harness.send({
        {"jsonrpc", "2.0"},
        {"id", 3},
        {"method", "initialize"},
        {"params", {{"protocolVersion", 2}}},
    });
    const Json negotiated = harness.wait_for([](const Json& message) {
        return is_response(message, 3);
    });
    REQUIRE(negotiated["result"]["protocolVersion"] == 1);
    harness.stop();
}

TEST_CASE("ACP server bounds active sessions", "[acp][session]") {
    TempWorkspace workspace;
    FakeProvider provider;
    ProtocolHarness harness{workspace.path(), provider, 1};
    harness.start();
    const std::string session_id = initialize_and_create(harness);

    harness.send({
        {"jsonrpc", "2.0"},
        {"id", 3},
        {"method", "session/new"},
        {"params", {
            {"cwd", harness.workspace()},
            {"mcpServers", Json::array()},
        }},
    });
    const Json full = harness.wait_for([](const Json& message) {
        return is_response(message, 3);
    });
    REQUIRE(full["error"]["code"] == -32000);

    harness.send({
        {"jsonrpc", "2.0"},
        {"id", 4},
        {"method", "session/load"},
        {"params", {
            {"sessionId", session_id},
            {"cwd", harness.workspace()},
            {"mcpServers", Json::array()},
        }},
    });
    const Json reloaded = harness.wait_for([](const Json& message) {
        return is_response(message, 4);
    });
    REQUIRE(reloaded["result"].is_object());
    harness.stop();
}

TEST_CASE("ACP server cannot override a denied command", "[acp][permission]") {
    TempWorkspace workspace;
    FakeProvider provider;
    provider.set_responses({
        "Unsafe\nRUN: rm protected.txt\n",
        "Conclusion: denied\nRUN: echo COMPLETE_TASK\n",
    });
    ProtocolHarness harness{workspace.path(), provider};
    harness.start();
    const std::string session_id = initialize_and_create(harness);
    harness.send({
        {"jsonrpc", "2.0"},
        {"id", 3},
        {"method", "session/prompt"},
        {"params", {
            {"sessionId", session_id},
            {"prompt", Json::array({{{"type", "text"}, {"text", "remove"}}})},
        }},
    });

    const Json completed = harness.wait_for([](const Json& message) {
        return is_response(message, 3);
    });
    REQUIRE(completed["result"]["stopReason"] == "end_turn");
    const auto messages = harness.messages();
    REQUIRE(std::any_of(messages.begin(), messages.end(), [](const Json& message) {
        return is_method(message, "session/update") &&
            message["params"]["update"].value("status", "") == "failed";
    }));
    REQUIRE_FALSE(std::any_of(
        messages.begin(),
        messages.end(),
        [](const Json& message) {
            return is_method(message, "session/request_permission");
        }));
    harness.stop();
}
