#include <catch2/catch_test_macros.hpp>

#include "tui/run_status.hpp"

#include <chrono>

TEST_CASE("run status uses stable Grok-style spinner frames", "[tui][run-status]") {
    REQUIRE(swe_agent::tui::run_spinner_frame(0) == "⠋");
    REQUIRE(swe_agent::tui::run_spinner_frame(7) == "⠧");
    REQUIRE(swe_agent::tui::run_spinner_frame(8) == "⠋");
}

TEST_CASE("run status formats elapsed time compactly", "[tui][run-status]") {
    using namespace std::chrono_literals;
    using swe_agent::tui::format_run_duration;

    REQUIRE(format_run_duration(500ms) == "0.5s");
    REQUIRE(format_run_duration(5200ms) == "5.2s");
    REQUIRE(format_run_duration(10s) == "10s");
    REQUIRE(format_run_duration(80s) == "1m20s");
    REQUIRE(format_run_duration(3725s) == "1h2m");
}

TEST_CASE("run status tracks turn and phase time independently", "[tui][run-status]") {
    using namespace std::chrono_literals;
    swe_agent::tui::RunStatusAnimation animation;
    const auto start = swe_agent::tui::RunStatusAnimation::TimePoint{} + 10s;

    animation.sync(true, 1, "Thinking", start, start);
    REQUIRE(animation.active());
    REQUIRE(animation.activity() == "Thinking");
    REQUIRE(animation.turn_elapsed(start + 2s) == 2s);
    REQUIRE(animation.phase_elapsed(start + 2s) == 2s);

    animation.sync(true, 1, "Running command", start, start + 2s);
    REQUIRE(animation.turn_elapsed(start + 3s) == 3s);
    REQUIRE(animation.phase_elapsed(start + 3s) == 1s);

    // 即使 UI 没来得及绘制 idle，新 task_id 也必须采用新任务的时间戳。
    animation.sync(true, 2, "Thinking", start + 10s, start + 10s);
    REQUIRE(animation.task_id() == 2);
    REQUIRE(animation.turn_elapsed(start + 11s) == 1s);

    animation.sync(false, 2, "Ready", start + 10s, start + 10s);
    REQUIRE_FALSE(animation.active());
    REQUIRE(animation.turn_elapsed(start + 5s) == 0s);
}

TEST_CASE("run status normalizes activity labels", "[tui][run-status]") {
    REQUIRE(swe_agent::tui::format_run_activity("") == "Working…");
    REQUIRE(swe_agent::tui::format_run_activity("Thinking") == "Thinking…");
    REQUIRE(swe_agent::tui::format_run_activity("Stopping…") == "Stopping…");
}
