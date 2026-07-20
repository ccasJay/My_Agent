#include <catch2/catch_test_macros.hpp>

#include "agent/agent_session.hpp"

namespace {

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
