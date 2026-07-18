#include <catch2/catch_test_macros.hpp>

#include "agent/agent_event.hpp"
#include "agent/agent_run_result.hpp"
#include "tui/tui_state.hpp"

#include <string>

namespace {

swe_agent::agent::AgentRunResult make_result(
    swe_agent::agent::AgentRunStatus status,
    std::size_t step = 0) {
    return {
        .status = status,
        .response = {},
        .step = step,
    };
}

}  // namespace

TEST_CASE("TUI state rejects empty and concurrent tasks", "[tui]") {
    swe_agent::tui::TuiState state{"test-model"};

    REQUIRE_FALSE(state.begin_task(""));
    REQUIRE_FALSE(state.begin_task("   \t"));
    REQUIRE(state.begin_task("first task"));
    REQUIRE(state.running());
    REQUIRE(state.task_id() == 1);
    REQUIRE_FALSE(state.begin_task("second task"));
    REQUIRE(state.logs().size() == 1);
    REQUIRE(state.logs()[0].content == "first task");
}

TEST_CASE("TUI state applies events and returns to ready", "[tui]") {
    using swe_agent::agent::AgentEvent;
    using swe_agent::agent::AgentEventType;

    swe_agent::tui::TuiState state{"test-model"};
    REQUIRE(state.begin_task("task"));
    REQUIRE(state.status_text() == "Thinking");

    state.apply_event(AgentEvent{
        .type = AgentEventType::Assistant,
        .step = 2,
        .content = "thinking",
    });
    state.apply_event(AgentEvent{
        .type = AgentEventType::Completed,
        .step = 2,
        .content = "done",
    });
    state.apply_result(make_result(
        swe_agent::agent::AgentRunStatus::Completed,
        2));

    REQUIRE_FALSE(state.running());
    REQUIRE(state.status() == swe_agent::tui::TuiStatus::Ready);
    REQUIRE(state.step() == 2);
    REQUIRE(state.logs().back().heading == "Final");
    REQUIRE(state.logs().back().content == "done");
}

TEST_CASE("TUI state reports the current agent activity", "[tui]") {
    using swe_agent::agent::AgentEvent;
    using swe_agent::agent::AgentEventType;
    using swe_agent::tui::TuiLogKind;

    swe_agent::tui::TuiState state{"test-model"};
    REQUIRE(state.begin_task("task"));

    state.apply_event(AgentEvent{
        .type = AgentEventType::CommandStarted,
        .command = "echo hello",
    });
    REQUIRE(state.status_text() == "Running command");
    REQUIRE(state.activity_text() == "Run echo hello");
    REQUIRE(state.logs().back().kind == TuiLogKind::Command);

    state.apply_event(AgentEvent{
        .type = AgentEventType::CommandFinished,
        .content = "hello",
        .command = "echo hello",
    });
    REQUIRE(state.status_text() == "Thinking");
    REQUIRE(state.activity_text() == "Thinking");
    REQUIRE(state.logs().back().kind == TuiLogKind::Observation);

    state.apply_event(AgentEvent{
        .type = AgentEventType::CommandStarted,
        .command = std::string(100, 'x'),
    });
    REQUIRE(state.activity_text().starts_with("Run "));
    REQUIRE(state.activity_text().ends_with("…"));
    REQUIRE(state.activity_text().size() < 100);
}

TEST_CASE("TUI state exposes stopping and error states", "[tui]") {
    swe_agent::tui::TuiState state{"test-model"};
    REQUIRE_FALSE(state.request_stop());

    REQUIRE(state.begin_task("task"));
    state.apply_event(swe_agent::agent::AgentEvent{
        .type = swe_agent::agent::AgentEventType::CommandStarted,
        .command = "sleep 1",
    });
    REQUIRE(state.request_stop());
    REQUIRE(state.status() == swe_agent::tui::TuiStatus::Stopping);
    REQUIRE(state.activity_text() == "Stopping");
    const auto stopping_started_at = state.activity_started_at();
    state.apply_event(swe_agent::agent::AgentEvent{
        .type = swe_agent::agent::AgentEventType::CommandFinished,
        .content = "done",
        .command = "sleep 1",
    });
    REQUIRE(state.activity_started_at() == stopping_started_at);
    const auto log_count = state.logs().size();
    REQUIRE(state.request_stop());
    REQUIRE(state.logs().size() == log_count);

    state.apply_event(swe_agent::agent::AgentEvent{
        .type = swe_agent::agent::AgentEventType::Stopped,
    });
    state.apply_result(make_result(
        swe_agent::agent::AgentRunStatus::Stopped));
    REQUIRE(state.status() == swe_agent::tui::TuiStatus::Stopped);

    REQUIRE(state.begin_task("retry"));
    state.fail_task("network failed");
    REQUIRE_FALSE(state.running());
    REQUIRE(state.status() == swe_agent::tui::TuiStatus::Error);
    REQUIRE(state.logs().back().content == "network failed");
}

TEST_CASE("TUI state preserves terminal reasons and accepts a new task", "[tui]") {
    using swe_agent::agent::AgentEvent;
    using swe_agent::agent::AgentEventType;
    using swe_agent::tui::TuiStatus;

    swe_agent::tui::TuiState state{"test-model"};
    REQUIRE(state.begin_task("limited task"));
    state.apply_event(AgentEvent{.type = AgentEventType::StepLimitReached});
    state.apply_result(make_result(
        swe_agent::agent::AgentRunStatus::StepLimitReached));

    REQUIRE_FALSE(state.running());
    REQUIRE(state.status() == TuiStatus::StepLimitReached);
    REQUIRE(state.begin_task("retry"));
    REQUIRE(state.task_id() == 2);

    state.apply_event(AgentEvent{.type = AgentEventType::EmptyResponse});
    state.apply_result(make_result(
        swe_agent::agent::AgentRunStatus::EmptyResponse));
    REQUIRE_FALSE(state.running());
    REQUIRE(state.status() == TuiStatus::EmptyResponse);
}
