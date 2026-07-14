#include <catch2/catch_test_macros.hpp>

#include "model/message.hpp"
#include "model/model.hpp"
#include "model/model_client.hpp"
#include "model/openai_format.hpp"

#include <string>
#include <type_traits>

// ---------------------------------------------------------------------------
// Note on OpenaiCompatible::query network path
// ---------------------------------------------------------------------------
// OpenaiCompatible::query uses http::HttpClient (libcurl) against config.base_url.
// Without HttpClient DI / mock, live network unit tests are intentionally omitted:
// no OpenAI calls, no .env, no API keys in CI.
//
// Covered offline instead:
//   - Provider concept (FakeProvider + OpenaiCompatible static_assert)
//   - Message / MSG / ModelConfig value types
//   - pure helpers role_to_string + build_request_body (JSON request shape)
// ---------------------------------------------------------------------------

using namespace swe_agent::model;

namespace {

// Minimal Provider for concept / polymorphic-style call-site checks.
struct FakeProvider {
    std::string last_content;

    ModelResponse query(const MSG& messages) {
        if (messages.empty()) {
            last_content.clear();
            return ModelResponse{"empty"};
        }
        last_content = messages.back().content;
        return ModelResponse{"fake:" + last_content};
    }
};

static_assert(Provider<FakeProvider>, "FakeProvider must satisfy Provider");
static_assert(Provider<OpenaiCompatible>,
              "OpenaiCompatible must satisfy Provider");

}  // namespace

TEST_CASE("FakeProvider satisfies Provider and returns canned response",
          "[model]") {
    FakeProvider provider;
    const MSG messages{
        Message{Role::User, "hello"},
    };

    const ModelResponse response = provider.query(messages);

    REQUIRE(response.content == "fake:hello");
    REQUIRE(provider.last_content == "hello");
}

TEST_CASE("Message and MSG construction basics", "[model]") {
    Message system_msg{Role::System, "you are helpful"};
    Message user_msg{Role::User, "hi"};
    Message assistant_msg{Role::Assistant, "hello"};
    Message tool_msg{Role::Tool, "tool-output"};

    REQUIRE(system_msg.role == Role::System);
    REQUIRE(system_msg.content == "you are helpful");
    REQUIRE(user_msg.role == Role::User);
    REQUIRE(assistant_msg.role == Role::Assistant);
    REQUIRE(tool_msg.role == Role::Tool);

    MSG history;
    REQUIRE(history.empty());

    history.push_back(system_msg);
    history.push_back(user_msg);
    REQUIRE(history.size() == 2);
    REQUIRE(history[0].role == Role::System);
    REQUIRE(history[1].content == "hi");

    const MSG braced{
        {Role::System, "sys"},
        {Role::User, "ask"},
    };
    REQUIRE(braced.size() == 2);
    REQUIRE(braced.front().content == "sys");
}

TEST_CASE("ModelConfig fields are assignable", "[model]") {
    ModelConfig config;
    config.base_url = "https://api.example.com/v1/chat/completions";
    config.api_key = "test-key-not-real";
    config.model_name = "gpt-test";

    REQUIRE(config.base_url == "https://api.example.com/v1/chat/completions");
    REQUIRE(config.api_key == "test-key-not-real");
    REQUIRE(config.model_name == "gpt-test");

    // Aggregate / copy semantics
    const ModelConfig copy = config;
    REQUIRE(copy.model_name == config.model_name);
    REQUIRE(copy.base_url == config.base_url);
}

TEST_CASE("role_to_string maps all Role values", "[model]") {
    REQUIRE(std::string{role_to_string(Role::System)} == "system");
    REQUIRE(std::string{role_to_string(Role::User)} == "user");
    REQUIRE(std::string{role_to_string(Role::Assistant)} == "assistant");
    REQUIRE(std::string{role_to_string(Role::Tool)} == "tool");
}

TEST_CASE("build_request_body produces OpenAI chat JSON shape", "[model]") {
    const ModelConfig config{
        .base_url = "https://unused.example/v1/chat/completions",
        .api_key = "unused",
        .model_name = "test-model",
    };

    const MSG messages{
        Message{Role::System, "be brief"},
        Message{Role::User, "2+2?"},
        Message{Role::Assistant, "4"},
        Message{Role::Tool, "calc=4"},
    };

    const auto body = build_request_body(config, messages);

    REQUIRE(body.at("model") == "test-model");
    REQUIRE(body.contains("messages"));
    REQUIRE(body.at("messages").is_array());
    REQUIRE(body.at("messages").size() == 4);

    REQUIRE(body["messages"][0]["role"] == "system");
    REQUIRE(body["messages"][0]["content"] == "be brief");
    REQUIRE(body["messages"][1]["role"] == "user");
    REQUIRE(body["messages"][1]["content"] == "2+2?");
    REQUIRE(body["messages"][2]["role"] == "assistant");
    REQUIRE(body["messages"][2]["content"] == "4");
    REQUIRE(body["messages"][3]["role"] == "tool");
    REQUIRE(body["messages"][3]["content"] == "calc=4");

    // api_key / base_url must not leak into request body
    REQUIRE_FALSE(body.contains("api_key"));
    REQUIRE_FALSE(body.contains("base_url"));
    REQUIRE_FALSE(body.contains("Authorization"));
}

TEST_CASE("build_request_body handles empty MSG", "[model]") {
    const ModelConfig config{
        .base_url = "https://unused.example",
        .api_key = "unused",
        .model_name = "m",
    };

    const auto body = build_request_body(config, MSG{});
    REQUIRE(body.at("model") == "m");
    REQUIRE(body.at("messages").is_array());
    REQUIRE(body.at("messages").empty());
}

TEST_CASE("OpenaiCompatible is constructible from ModelConfig", "[model]") {
    // Construction only — do not call query() (would hit network/curl).
    const ModelConfig config{
        .base_url = "https://example.invalid/v1/chat/completions",
        .api_key = "no-key",
        .model_name = "no-model",
    };

    OpenaiCompatible client{config};
    static_assert(std::is_same_v<decltype(client.query(MSG{})), ModelResponse>);
    SUCCEED("OpenaiCompatible constructed; query left uncalled (no network)");
}
