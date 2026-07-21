#include <catch2/catch_test_macros.hpp>

#include "agent/agent_event.hpp"
#include "tui/tui_session.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace {

swe_agent::agent::PolicyContext test_policy_context() {
    return {
        .working_dir = "/tmp/project",
        .workspace_root = "/tmp/project",
    };
}

}  // namespace

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
        test_policy_context(),
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
        test_policy_context(),
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
        test_policy_context(),
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

TEST_CASE("TUI session approves a pending command", "[tui][session][authorization]") {
    using swe_agent::agent::AgentRunResult;
    using swe_agent::agent::AgentRunStatus;
    using swe_agent::agent::CommandAction;
    using swe_agent::agent::CommandDecision;
    using swe_agent::agent::CommandRequest;

    std::mutex notification_mutex;
    std::condition_variable notification;
    CommandDecision received{
        .action = CommandAction::Stop,
        .reason = "Not decided",
    };

    swe_agent::tui::TuiSession session{
        "test-model",
        [&](const std::string&, const swe_agent::agent::AgentRunOptions& options) {
            received = options.authorizer(CommandRequest{
                .step = 2,
                .command = "echo approved",
            });
            return AgentRunResult{
                .status = AgentRunStatus::Completed,
                .response = {},
                .step = 2,
            };
        },
        [&] {
            std::lock_guard lock{notification_mutex};
            notification.notify_all();
        },
        test_policy_context(),
    };

    REQUIRE(session.start("task"));
    {
        std::unique_lock lock{notification_mutex};
        REQUIRE(notification.wait_for(lock, std::chrono::seconds{1}, [&] {
            return session.awaiting_command_approval();
        }));
    }
    const auto pending = session.snapshot(0);
    REQUIRE(pending.awaiting_approval);
    REQUIRE(pending.pending_command == "echo approved");
    REQUIRE(pending.command_mode == swe_agent::tui::CommandMode::Review);
    REQUIRE_FALSE(session.toggle_command_mode());
    REQUIRE(session.approve_command());
    REQUIRE_FALSE(session.approve_command());
    {
        std::unique_lock lock{notification_mutex};
        REQUIRE(notification.wait_for(lock, std::chrono::seconds{1}, [&] {
            return !session.running();
        }));
    }
    session.stop_and_join();

    REQUIRE(received.action == CommandAction::Approve);
    REQUIRE_FALSE(session.snapshot(0).awaiting_approval);
}

TEST_CASE("TUI session auto mode bypasses command review", "[tui][session][authorization]") {
    using swe_agent::agent::AgentRunResult;
    using swe_agent::agent::AgentRunStatus;
    using swe_agent::agent::CommandAction;
    using swe_agent::agent::CommandDecision;
    using swe_agent::agent::CommandRequest;
    using swe_agent::tui::CommandMode;

    std::mutex notification_mutex;
    std::condition_variable notification;
    CommandDecision received{.action = CommandAction::Stop};

    swe_agent::tui::TuiSession session{
        "test-model",
        [&](const std::string&, const swe_agent::agent::AgentRunOptions& options) {
            received = options.authorizer(CommandRequest{
                .command = "echo automatic",
            });
            return AgentRunResult{
                .status = AgentRunStatus::Completed,
                .response = {},
            };
        },
        [&] {
            std::lock_guard lock{notification_mutex};
            notification.notify_all();
        },
        test_policy_context(),
    };

    REQUIRE(session.command_mode() == CommandMode::Review);
    REQUIRE(session.toggle_command_mode());
    REQUIRE(session.command_mode() == CommandMode::Auto);
    REQUIRE(session.start("task"));
    {
        std::unique_lock lock{notification_mutex};
        REQUIRE(notification.wait_for(lock, std::chrono::seconds{1}, [&] {
            return !session.running();
        }));
    }
    session.stop_and_join();

    REQUIRE(received.action == CommandAction::Approve);
    const auto snapshot = session.snapshot(0);
    REQUIRE(snapshot.command_mode == CommandMode::Auto);
    REQUIRE_FALSE(snapshot.awaiting_approval);
    REQUIRE(session.toggle_command_mode());
    REQUIRE(session.command_mode() == CommandMode::Review);
}

TEST_CASE("TUI session rejects a pending command", "[tui][session][authorization]") {
    using swe_agent::agent::AgentRunResult;
    using swe_agent::agent::AgentRunStatus;
    using swe_agent::agent::CommandAction;
    using swe_agent::agent::CommandDecision;
    using swe_agent::agent::CommandRequest;

    std::mutex notification_mutex;
    std::condition_variable notification;
    CommandDecision received{.action = CommandAction::Approve};

    // 使用未列入 deny 的命令，才能走到人工 Reject 路径。
    swe_agent::tui::TuiSession session{
        "test-model",
        [&](const std::string&, const swe_agent::agent::AgentRunOptions& options) {
            received = options.authorizer(CommandRequest{
                .command = "echo example.txt",
            });
            return AgentRunResult{
                .status = AgentRunStatus::Completed,
                .response = {},
            };
        },
        [&] {
            std::lock_guard lock{notification_mutex};
            notification.notify_all();
        },
        test_policy_context(),
    };

    REQUIRE(session.start("task"));
    {
        std::unique_lock lock{notification_mutex};
        REQUIRE(notification.wait_for(lock, std::chrono::seconds{1}, [&] {
            return session.awaiting_command_approval();
        }));
    }
    REQUIRE(session.reject_command("Not allowed"));
    {
        std::unique_lock lock{notification_mutex};
        REQUIRE(notification.wait_for(lock, std::chrono::seconds{1}, [&] {
            return !session.running();
        }));
    }
    session.stop_and_join();

    REQUIRE(received.action == CommandAction::Reject);
    REQUIRE(received.reason == "Not allowed");
    const auto snapshot = session.snapshot(0);
    REQUIRE_FALSE(snapshot.awaiting_approval);
    REQUIRE(snapshot.new_logs.back().heading == "Command rejected");
}

TEST_CASE(
    "TUI session policy denies dangerous commands without approval UI",
    "[tui][session][authorization][policy]") {
    using swe_agent::agent::AgentRunResult;
    using swe_agent::agent::AgentRunStatus;
    using swe_agent::agent::CommandAction;
    using swe_agent::agent::CommandDecision;
    using swe_agent::agent::CommandRequest;
    using swe_agent::tui::CommandMode;

    std::mutex notification_mutex;
    std::condition_variable notification;
    CommandDecision received{.action = CommandAction::Approve};

    swe_agent::tui::TuiSession session{
        "test-model",
        [&](const std::string&, const swe_agent::agent::AgentRunOptions& options) {
            received = options.authorizer(CommandRequest{
                .command = "rm example.txt",
            });
            return AgentRunResult{
                .status = AgentRunStatus::Completed,
                .response = {},
            };
        },
        [&] {
            std::lock_guard lock{notification_mutex};
            notification.notify_all();
        },
        test_policy_context(),
    };

    REQUIRE(session.toggle_command_mode());
    REQUIRE(session.command_mode() == CommandMode::Auto);
    REQUIRE(session.start("task"));
    {
        std::unique_lock lock{notification_mutex};
        REQUIRE(notification.wait_for(lock, std::chrono::seconds{1}, [&] {
            return !session.running();
        }));
    }
    session.stop_and_join();

    REQUIRE(received.action == CommandAction::Reject);
    REQUIRE_FALSE(received.reason.empty());
    REQUIRE_FALSE(session.snapshot(0).awaiting_approval);
}

TEST_CASE("TUI session stop releases command approval", "[tui][session][authorization]") {
    using swe_agent::agent::AgentRunResult;
    using swe_agent::agent::AgentRunStatus;
    using swe_agent::agent::CommandAction;
    using swe_agent::agent::CommandDecision;
    using swe_agent::agent::CommandRequest;

    std::mutex notification_mutex;
    std::condition_variable notification;
    CommandDecision received{.action = CommandAction::Approve};

    swe_agent::tui::TuiSession session{
        "test-model",
        [&](const std::string&, const swe_agent::agent::AgentRunOptions& options) {
            received = options.authorizer(CommandRequest{
                .command = "sleep 10",
            });
            return AgentRunResult{
                .status = AgentRunStatus::Stopped,
                .response = {},
            };
        },
        [&] {
            std::lock_guard lock{notification_mutex};
            notification.notify_all();
        },
        test_policy_context(),
    };

    REQUIRE(session.start("task"));
    {
        std::unique_lock lock{notification_mutex};
        REQUIRE(notification.wait_for(lock, std::chrono::seconds{1}, [&] {
            return session.awaiting_command_approval();
        }));
    }
    REQUIRE(session.request_stop());
    {
        std::unique_lock lock{notification_mutex};
        REQUIRE(notification.wait_for(lock, std::chrono::seconds{1}, [&] {
            return !session.running();
        }));
    }
    session.stop_and_join();

    REQUIRE(received.action == CommandAction::Stop);
    REQUIRE_FALSE(session.snapshot(0).awaiting_approval);
}

TEST_CASE("TUI session can replace idle logs from storage", "[tui][session]") {
    swe_agent::tui::TuiSession session{
        "old-model",
        [](const std::string&, const swe_agent::agent::AgentRunOptions&) {
            return swe_agent::agent::AgentRunResult{};
        },
        [] {},
        test_policy_context(),
    };
    const swe_agent::agent::SessionSnapshot restored{
        .metadata = {
            .id = "abcdef123456",
            .title = "saved task",
            .model_name = "restored-model",
        },
        .messages = {
            {
                .role = swe_agent::model::Role::System,
                .kind = swe_agent::agent::SessionMessageKind::System,
                .content = "system",
            },
            {
                .sequence = 1,
                .role = swe_agent::model::Role::User,
                .kind = swe_agent::agent::SessionMessageKind::UserPrompt,
                .content = "saved task",
            },
        },
    };

    REQUIRE(session.load_session(restored));
    session.append_notice("Sessions", "abcdef12 saved task");

    const auto snapshot = session.snapshot(0);
    REQUIRE(snapshot.model_name == "restored-model");
    REQUIRE(snapshot.task_id == 1);
    REQUIRE(snapshot.new_logs.size() == 3);
    REQUIRE(snapshot.new_logs.back().heading == "Sessions");
}
