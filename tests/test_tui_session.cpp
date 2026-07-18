#include <catch2/catch_test_macros.hpp>

#include "agent/agent_event.hpp"
#include "tui/tui_session.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

TEST_CASE("TUI session runs a task and exposes a consistent snapshot", "[tui][session]") {
    std::mutex notification_mutex;
    std::condition_variable notification;

    swe_agent::tui::TuiSession session{
        "test-model",
        [](const std::string&, const swe_agent::agent::AgentRunOptions& options) {
            options.on_event(swe_agent::agent::AgentEvent{
                .type = swe_agent::agent::AgentEventType::Assistant,
                .content = "working",
            });
            options.on_event(swe_agent::agent::AgentEvent{
                .type = swe_agent::agent::AgentEventType::Completed,
                .content = "done",
            });
            return swe_agent::agent::AgentRunResult{
                .status = swe_agent::agent::AgentRunStatus::Completed,
                .response = {},
                .step = 0,
            };
        },
        [&] {
            std::lock_guard lock{notification_mutex};
            notification.notify_all();
        },
    };

    REQUIRE(session.start("task"));
    {
        std::unique_lock lock{notification_mutex};
        REQUIRE(notification.wait_for(lock, std::chrono::seconds{1}, [&] {
            return !session.running();
        }));
    }
    session.stop_and_join();

    const auto snapshot = session.snapshot(0);
    REQUIRE_FALSE(snapshot.running);
    REQUIRE(snapshot.status_text == "Ready");
    REQUIRE(snapshot.model_name == "test-model");
    REQUIRE(snapshot.logs_changed);
    REQUIRE(snapshot.new_logs.size() == 3);
    REQUIRE(snapshot.new_logs.back().heading == "Final");

    const auto delta = session.snapshot(1);
    REQUIRE(delta.logs_changed);
    REQUIRE(delta.new_logs.size() == 2);

    const auto unchanged = session.snapshot(snapshot.log_revision);
    REQUIRE_FALSE(unchanged.logs_changed);
    REQUIRE(unchanged.new_logs.empty());

    const auto resync = session.snapshot(snapshot.log_revision + 1);
    REQUIRE(resync.logs_changed);
    REQUIRE(resync.full_resync);
    REQUIRE(resync.new_logs.size() == snapshot.new_logs.size());
}

TEST_CASE("TUI session stop is cooperative and idempotent", "[tui][session]") {
    std::atomic_bool runner_started{false};
    std::mutex notification_mutex;
    std::condition_variable notification;

    swe_agent::tui::TuiSession session{
        "test-model",
        [&](const std::string&, const swe_agent::agent::AgentRunOptions& options) {
            {
                std::lock_guard lock{notification_mutex};
                runner_started.store(true);
            }
            notification.notify_all();
            while (!options.stop_token.stop_requested()) {
                std::this_thread::yield();
            }
            options.on_event(swe_agent::agent::AgentEvent{
                .type = swe_agent::agent::AgentEventType::Stopped,
            });
            return swe_agent::agent::AgentRunResult{
                .status = swe_agent::agent::AgentRunStatus::Stopped,
                .response = {},
                .step = 0,
            };
        },
        [&] {
            std::lock_guard lock{notification_mutex};
            notification.notify_all();
        },
    };

    REQUIRE(session.start("task"));
    {
        std::unique_lock lock{notification_mutex};
        REQUIRE(notification.wait_for(lock, std::chrono::seconds{1}, [&] {
            return runner_started.load();
        }));
    }

    REQUIRE(session.request_stop());
    (void)session.request_stop();
    session.stop_and_join();

    const auto snapshot = session.snapshot(0);
    REQUIRE_FALSE(snapshot.running);
    REQUIRE(snapshot.status_text == "Stopped");

    std::size_t stopping_messages = 0;
    for (const auto& log : snapshot.new_logs) {
        if (log.content.find("Stopping after") != std::string::npos) {
            ++stopping_messages;
        }
    }
    REQUIRE(stopping_messages == 1);
}

TEST_CASE("TUI session stays busy until the runner returns", "[tui][session]") {
    std::atomic_bool release_runner{false};
    std::mutex notification_mutex;
    std::condition_variable notification;
    bool terminal_event_emitted = false;

    swe_agent::tui::TuiSession session{
        "test-model",
        [&](const std::string&, const swe_agent::agent::AgentRunOptions& options) {
            options.on_event(swe_agent::agent::AgentEvent{
                .type = swe_agent::agent::AgentEventType::Completed,
                .content = "done",
            });
            {
                std::lock_guard lock{notification_mutex};
                terminal_event_emitted = true;
            }
            notification.notify_all();

            while (!release_runner.load()) {
                std::this_thread::yield();
            }

            return swe_agent::agent::AgentRunResult{
                .status = swe_agent::agent::AgentRunStatus::Completed,
                .response = {},
                .step = 1,
            };
        },
        [&] {
            std::lock_guard lock{notification_mutex};
            notification.notify_all();
        },
    };

    REQUIRE(session.start("first task"));
    {
        std::unique_lock lock{notification_mutex};
        REQUIRE(notification.wait_for(lock, std::chrono::seconds{1}, [&] {
            return terminal_event_emitted;
        }));
    }

    REQUIRE(session.running());
    REQUIRE_FALSE(session.start("second task"));

    release_runner.store(true);
    {
        std::unique_lock lock{notification_mutex};
        REQUIRE(notification.wait_for(lock, std::chrono::seconds{1}, [&] {
            return !session.running();
        }));
    }
    session.stop_and_join();
    REQUIRE(session.snapshot(0).status_text == "Ready");
}
