#include "acp/prompt_controller.hpp"

#include "agent/assistant_text.hpp"
#include "agent/command_authorization.hpp"
#include "agent/shell.hpp"

#include <exception>
#include <iostream>
#include <utility>

namespace swe_agent::acp {
namespace {

constexpr int kInternalError = -32603;
constexpr std::string_view kCompletionCommand = "echo COMPLETE_TASK";

std::string tool_title(std::string_view command) {
    constexpr std::size_t kMaxTitleBytes = 160;
    std::string title = "$ " + std::string{command};
    if (title.size() > kMaxTitleBytes) {
        title.resize(kMaxTitleBytes - 3);
        title += "...";
    }
    return title;
}

std::string stop_reason(agent::AgentRunStatus status) {
    switch (status) {
    case agent::AgentRunStatus::Completed:
        return "end_turn";
    case agent::AgentRunStatus::Stopped:
        return "cancelled";
    case agent::AgentRunStatus::StepLimitReached:
        return "max_turn_requests";
    case agent::AgentRunStatus::EmptyResponse:
        return "refusal";
    }
    return "refusal";
}

}  // namespace

struct AcpPromptController::PromptState {
    enum class ToolStatus {
        Pending,
        InProgress,
        Terminal,
    };

    struct ToolState {
        std::string id;
        ToolStatus status{ToolStatus::Pending};
    };

    Json request_id;
    std::string session_id;
    agent::StopSource stop_source;
    std::atomic_bool finished{false};

    std::mutex permission_mutex;
    std::condition_variable permission_cv;
    std::optional<Json> permission_request_id;
    std::optional<Json> permission_response;

    std::unordered_map<std::size_t, ToolState> tool_calls;
};

AcpPromptController::AcpPromptController(
    JsonRpcConnection& connection,
    std::chrono::milliseconds permission_timeout)
    : connection_(connection),
      permission_timeout_(permission_timeout) {}

AcpPromptController::~AcpPromptController() {
    stop_and_join();
}

bool AcpPromptController::start(
    Json request_id,
    std::string prompt,
    AcpActiveSession active_session) {
    reap_finished();

    auto state = std::make_shared<PromptState>();
    state->request_id = std::move(request_id);
    state->session_id = active_session.session->id();
    {
        std::lock_guard lock{mutex_};
        if (active_) {
            return false;
        }
        active_ = state;
        try {
            worker_ = std::thread(
                [this,
                 state,
                 prompt = std::move(prompt),
                 active_session = std::move(active_session)]() mutable {
                    run_prompt(
                        state,
                        std::move(prompt),
                        std::move(active_session));
                });
        } catch (...) {
            active_.reset();
            throw;
        }
    }
    return true;
}

bool AcpPromptController::cancel(std::string_view session_id) {
    std::shared_ptr<PromptState> state;
    {
        std::lock_guard lock{mutex_};
        if (!active_ || active_->session_id != session_id) {
            return false;
        }
        state = active_;
    }
    state->stop_source.request_stop();
    state->permission_cv.notify_all();
    return true;
}

bool AcpPromptController::cancel_and_wait(std::string_view session_id) {
    std::shared_ptr<PromptState> state;
    std::thread worker;
    {
        std::lock_guard lock{mutex_};
        if (!active_ || active_->session_id != session_id) {
            return false;
        }
        state = active_;
        active_.reset();
        worker = std::move(worker_);
    }
    state->stop_source.request_stop();
    state->permission_cv.notify_all();
    if (worker.joinable()) {
        worker.join();
    }
    return true;
}

bool AcpPromptController::is_running(std::string_view session_id) const {
    std::lock_guard lock{mutex_};
    return active_ && active_->session_id == session_id &&
        !active_->finished.load();
}

bool AcpPromptController::handle_response(const Json& message) {
    std::shared_ptr<PromptState> state;
    {
        std::lock_guard lock{mutex_};
        state = active_;
    }
    if (!state || !message.contains("id")) {
        return false;
    }

    {
        std::lock_guard lock{state->permission_mutex};
        if (!state->permission_request_id.has_value() ||
            *state->permission_request_id != message["id"]) {
            return false;
        }
        state->permission_response = message;
    }
    state->permission_cv.notify_all();
    return true;
}

void AcpPromptController::stop_and_join() {
    std::shared_ptr<PromptState> state;
    std::thread worker;
    {
        std::lock_guard lock{mutex_};
        state = active_;
        active_.reset();
        worker = std::move(worker_);
    }
    if (state) {
        state->stop_source.request_stop();
        state->permission_cv.notify_all();
    }
    if (worker.joinable()) {
        worker.join();
    }
}

void AcpPromptController::run_prompt(
    const std::shared_ptr<PromptState>& state,
    std::string prompt,
    AcpActiveSession active_session) {
    try {
        agent::AgentRunOptions options;
        options.stop_token = state->stop_source.token();
        options.shell_executor = [workspace = active_session.workspace,
                                  stop_token = options.stop_token](
                                     const std::string& command) {
            return agent::run_shell(command, workspace, stop_token);
        };
        options.authorizer = [this, state, workspace = active_session.workspace](
                                 const agent::CommandRequest& request) {
            return authorize_command(state, request, workspace);
        };
        options.on_event = [this, state](const agent::AgentEvent& event) {
            handle_event(state, event);
        };

        const agent::AgentRunResult result =
            active_session.session->submit(std::move(prompt), options);
        if (result.status == agent::AgentRunStatus::Stopped) {
            finalize_unfinished_tools(state, "Tool call cancelled.");
        } else {
            finalize_unfinished_tools(state, "Tool call did not finish.");
        }
        connection_.send_result(state->request_id, {
            {"stopReason", stop_reason(result.status)},
        });
    } catch (const std::exception& error) {
        std::cerr << "ACP prompt failed: " << error.what() << '\n';
        try {
            finalize_unfinished_tools(state, "Tool call failed.");
            connection_.send_error(
                state->request_id,
                kInternalError,
                "Internal error");
        } catch (const std::exception& write_error) {
            std::cerr << "ACP prompt response failed: "
                      << write_error.what() << '\n';
        }
    }
    state->finished.store(true);
}

agent::CommandDecision AcpPromptController::authorize_command(
    const std::shared_ptr<PromptState>& state,
    const agent::CommandRequest& request,
    const std::string& workspace) {
    const std::string tool_call_id = "tool-" +
        std::to_string(next_tool_call_id_.fetch_add(1));
    state->tool_calls[request.step] = {
        .id = tool_call_id,
        .status = PromptState::ToolStatus::Pending,
    };
    connection_.send_notification("session/update", {
        {"sessionId", state->session_id},
        {"update", {
            {"sessionUpdate", "tool_call"},
            {"toolCallId", tool_call_id},
            {"title", tool_title(request.command)},
            {"kind", "execute"},
            {"status", "pending"},
            {"rawInput", {{"command", request.command}}},
        }},
    });

    const agent::PolicyContext policy_context{
        .working_dir = workspace,
        .workspace_root = workspace,
    };
    return agent::authorize_command(
        request,
        policy_context,
        false,
        [this, state, tool_call_id](
            const agent::CommandRequest& review_request) {
            return request_permission(
                state,
                review_request,
                tool_call_id);
        });
}

agent::CommandDecision AcpPromptController::request_permission(
    const std::shared_ptr<PromptState>& state,
    const agent::CommandRequest& request,
    std::string_view tool_call_id) {
    const Json request_id = connection_.next_request_id();
    {
        std::lock_guard lock{state->permission_mutex};
        state->permission_request_id = request_id;
        state->permission_response.reset();
    }
    connection_.send_request(request_id, "session/request_permission", {
        {"sessionId", state->session_id},
        {"toolCall", {
            {"toolCallId", tool_call_id},
            {"title", tool_title(request.command)},
            {"kind", "execute"},
            {"status", "pending"},
            {"rawInput", {{"command", request.command}}},
        }},
        {"options", {
            {
                {"optionId", "allow-once"},
                {"name", "Allow once"},
                {"kind", "allow_once"},
            },
            {
                {"optionId", "reject-once"},
                {"name", "Reject"},
                {"kind", "reject_once"},
            },
        }},
    });

    Json response;
    {
        std::unique_lock lock{state->permission_mutex};
        const bool resolved = state->permission_cv.wait_for(
            lock,
            permission_timeout_,
            [&] {
                return state->permission_response.has_value() ||
                    state->stop_source.token().stop_requested();
            });
        if (!resolved) {
            state->permission_request_id.reset();
            state->permission_response.reset();
            return {
                .action = agent::CommandAction::Stop,
                .reason = "Permission request timed out.",
            };
        }
        if (state->stop_source.token().stop_requested()) {
            state->permission_request_id.reset();
            state->permission_response.reset();
            return {
                .action = agent::CommandAction::Stop,
                .reason = "Stop requested.",
            };
        }
        response = std::move(*state->permission_response);
        state->permission_request_id.reset();
        state->permission_response.reset();
    }

    if (response.contains("error")) {
        return {
            .action = agent::CommandAction::Reject,
            .rule_id = "permission_error",
            .reason = "The client could not resolve the permission request.",
        };
    }
    const Json outcome = response.value("result", Json::object())
        .value("outcome", Json::object());
    if (!outcome.is_object() || !outcome.contains("outcome") ||
        !outcome["outcome"].is_string()) {
        return {
            .action = agent::CommandAction::Reject,
            .rule_id = "invalid_permission_response",
            .reason = "The client returned an invalid permission response.",
        };
    }
    const std::string outcome_type = outcome["outcome"].get<std::string>();
    if (outcome_type == "cancelled") {
        return {
            .action = agent::CommandAction::Stop,
            .reason = "Permission request cancelled.",
        };
    }
    if (outcome_type == "selected" && outcome.contains("optionId") &&
        outcome["optionId"].is_string()) {
        const std::string option_id = outcome["optionId"].get<std::string>();
        if (option_id == "allow-once") {
            return {.action = agent::CommandAction::Approve};
        }
        if (option_id == "reject-once") {
            return {
                .action = agent::CommandAction::Reject,
                .rule_id = "user_rejected",
                .reason = "The user rejected this command.",
            };
        }
    }
    return {
        .action = agent::CommandAction::Reject,
        .rule_id = "invalid_permission_response",
        .reason = "The client selected an unknown permission option.",
    };
}

void AcpPromptController::handle_event(
    const std::shared_ptr<PromptState>& state,
    const agent::AgentEvent& event) {
    using agent::AgentEventType;

    if (event.type == AgentEventType::Assistant ||
        event.type == AgentEventType::Completed) {
        const std::string content = agent::strip_run_lines(event.content);
        if (agent::has_visible_text(content)) {
            connection_.send_notification("session/update", {
                {"sessionId", state->session_id},
                {"update", {
                    {"sessionUpdate", "agent_message_chunk"},
                    {"content", {
                        {"type", "text"},
                        {"text", content},
                    }},
                }},
            });
        }
        return;
    }

    if (event.command.empty() || event.command == kCompletionCommand) {
        return;
    }
    const auto tool = state->tool_calls.find(event.step);
    if (tool == state->tool_calls.end()) {
        return;
    }

    if (event.type == AgentEventType::CommandStarted) {
        connection_.send_notification("session/update", {
            {"sessionId", state->session_id},
            {"update", {
                {"sessionUpdate", "tool_call_update"},
                {"toolCallId", tool->second.id},
                {"status", "in_progress"},
            }},
        });
        tool->second.status = PromptState::ToolStatus::InProgress;
    } else if (event.type == AgentEventType::CommandFinished ||
               event.type == AgentEventType::CommandRejected) {
        const bool succeeded =
            event.type == AgentEventType::CommandFinished &&
            event.command_succeeded.value_or(false);
        Json update{
            {"sessionUpdate", "tool_call_update"},
            {"toolCallId", tool->second.id},
            {"status", succeeded ? "completed" : "failed"},
        };
        if (!event.content.empty()) {
            update["content"] = Json::array({
                {
                    {"type", "content"},
                    {"content", {
                        {"type", "text"},
                        {"text", event.content},
                    }},
                },
            });
        }
        connection_.send_notification("session/update", {
            {"sessionId", state->session_id},
            {"update", std::move(update)},
        });
        tool->second.status = PromptState::ToolStatus::Terminal;
    }
}

void AcpPromptController::finalize_unfinished_tools(
    const std::shared_ptr<PromptState>& state,
    std::string_view message) {
    for (auto& [step, tool] : state->tool_calls) {
        (void)step;
        if (tool.status == PromptState::ToolStatus::Terminal) {
            continue;
        }
        connection_.send_notification("session/update", {
            {"sessionId", state->session_id},
            {"update", {
                {"sessionUpdate", "tool_call_update"},
                {"toolCallId", tool.id},
                {"status", "failed"},
                {"content", Json::array({
                    {
                        {"type", "content"},
                        {"content", {
                            {"type", "text"},
                            {"text", message},
                        }},
                    },
                })},
            }},
        });
        tool.status = PromptState::ToolStatus::Terminal;
    }
}

void AcpPromptController::reap_finished() {
    std::thread worker;
    {
        std::lock_guard lock{mutex_};
        if (!active_ || !active_->finished.load()) {
            return;
        }
        active_.reset();
        worker = std::move(worker_);
    }
    if (worker.joinable()) {
        worker.join();
    }
}

}  // namespace swe_agent::acp
