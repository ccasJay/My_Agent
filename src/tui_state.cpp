#include "tui/tui_state.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace swe_agent::tui {
namespace {

bool is_blank(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
}

std::string compact_activity_detail(std::string_view text) {
    text = text.substr(0, text.find_first_of("\r\n"));
    constexpr std::size_t kMaxGlyphs = 48;
    std::size_t offset = 0;
    std::size_t glyphs = 0;
    while (offset < text.size() && glyphs < kMaxGlyphs) {
        const unsigned char lead = static_cast<unsigned char>(text[offset]);
        std::size_t bytes = 1;
        if ((lead & 0xE0U) == 0xC0U) {
            bytes = 2;
        } else if ((lead & 0xF0U) == 0xE0U) {
            bytes = 3;
        } else if ((lead & 0xF8U) == 0xF0U) {
            bytes = 4;
        }
        if (offset + bytes > text.size()) {
            bytes = 1;
        }
        offset += bytes;
        ++glyphs;
    }

    std::string result{text.substr(0, offset)};
    if (offset < text.size()) {
        result += "…";
    }
    return result;
}

}  // namespace

TuiState::TuiState(std::string model_name)
    : model_name_(std::move(model_name)) {}

bool TuiState::begin_task(std::string_view task) {
    if (running() || task.empty() || is_blank(task)) {
        return false;
    }

    status_ = TuiStatus::Running;
    activity_ = TuiActivity::Thinking;
    activity_detail_.clear();
    pending_command_.clear();
    turn_started_at_ = Clock::now();
    activity_started_at_ = turn_started_at_;
    step_ = 0;
    ++task_count_;
    append(
        TuiLogKind::Task,
        "Task " + std::to_string(task_count_),
        std::string{task});
    return true;
}

void TuiState::begin_command_approval(
    const agent::CommandRequest& request) {
    if (status_ != TuiStatus::Running) {
        return;
    }
    activity_ = TuiActivity::AwaitingApproval;
    activity_detail_ = compact_activity_detail(request.command);
    pending_command_ = request.command;
    step_ = request.step;
    activity_started_at_ = Clock::now();
}

void TuiState::resolve_command_approval(
    const agent::CommandDecision& decision) {
    (void)decision;
    if (pending_command_.empty()) {
        return;
    }

    pending_command_.clear();
    activity_detail_.clear();
    if (status_ == TuiStatus::Running) {
        activity_ = TuiActivity::Thinking;
        activity_started_at_ = Clock::now();
    }
}

bool TuiState::request_stop() {
    if (status_ == TuiStatus::Stopping) {
        return true;
    }
    if (status_ != TuiStatus::Running) {
        return false;
    }
    status_ = TuiStatus::Stopping;
    activity_started_at_ = Clock::now();
    append(
        TuiLogKind::System,
        "System",
        "Stopping after the current model request or command...");
    return true;
}

void TuiState::apply_event(const agent::AgentEvent& event) {
    step_ = event.step;

    switch (event.type) {
    case agent::AgentEventType::Assistant:
        append(TuiLogKind::Assistant, "Assistant", event.content);
        break;
    case agent::AgentEventType::FormatError:
        if (status_ != TuiStatus::Stopping &&
            activity_ != TuiActivity::Thinking) {
            activity_started_at_ = Clock::now();
        }
        activity_ = TuiActivity::Thinking;
        activity_detail_.clear();
        append(TuiLogKind::System, "Format error", event.content);
        break;
    case agent::AgentEventType::CommandStarted:
        activity_ = TuiActivity::RunningCommand;
        activity_detail_ = compact_activity_detail(event.command);
        if (status_ != TuiStatus::Stopping) {
            activity_started_at_ = Clock::now();
            status_ = TuiStatus::Running;
        }
        append(TuiLogKind::Command, "Command", "$ " + event.command);
        break;
    case agent::AgentEventType::CommandFinished:
        activity_ = TuiActivity::Thinking;
        activity_detail_.clear();
        if (status_ != TuiStatus::Stopping) {
            activity_started_at_ = Clock::now();
            status_ = TuiStatus::Running;
        }
        append(TuiLogKind::Observation, "Observation", event.content);
        break;
    case agent::AgentEventType::CommandRejected: {
        activity_ = TuiActivity::Thinking;
        activity_detail_.clear();
        if (status_ != TuiStatus::Stopping) {
            activity_started_at_ = Clock::now();
            status_ = TuiStatus::Running;
        }
        std::string content = "$ " + event.command;
        if (!event.rule_id.empty()) {
            content += "\n规则：" + event.rule_id;
        }
        if (!event.content.empty()) {
            content += "\n原因：" + event.content;
        }
        append(TuiLogKind::System, "命令已拒绝", std::move(content));
        break;
    }
    case agent::AgentEventType::Completed:
        append(TuiLogKind::Final, "Final", event.content);
        break;
    case agent::AgentEventType::Stopped:
        append(TuiLogKind::System, "System", "Task stopped.");
        break;
    case agent::AgentEventType::StepLimitReached:
        append(TuiLogKind::System, "System", "Step limit reached.");
        break;
    case agent::AgentEventType::EmptyResponse:
        append(
            TuiLogKind::System,
            "System",
            "Model returned an empty response.");
        break;
    }
}

void TuiState::apply_result(const agent::AgentRunResult& result) {
    step_ = result.step;
    activity_ = TuiActivity::Idle;
    activity_detail_.clear();
    pending_command_.clear();

    switch (result.status) {
    case agent::AgentRunStatus::Completed:
        status_ = TuiStatus::Ready;
        break;
    case agent::AgentRunStatus::Stopped:
        status_ = TuiStatus::Stopped;
        break;
    case agent::AgentRunStatus::StepLimitReached:
        status_ = TuiStatus::StepLimitReached;
        break;
    case agent::AgentRunStatus::EmptyResponse:
        status_ = TuiStatus::EmptyResponse;
        break;
    }
}

void TuiState::fail_task(std::string message) {
    status_ = TuiStatus::Error;
    activity_ = TuiActivity::Idle;
    activity_detail_.clear();
    pending_command_.clear();
    append(TuiLogKind::Error, "Error", std::move(message));
}

void TuiState::load_session(const agent::SessionSnapshot& snapshot) {
    logs_.clear();
    task_count_ = 0;
    step_ = 0;
    log_revision_ = 0;
    status_ = TuiStatus::Ready;
    activity_ = TuiActivity::Idle;
    activity_detail_.clear();
    pending_command_.clear();
    turn_started_at_ = {};
    activity_started_at_ = {};
    model_name_ = snapshot.metadata.model_name;

    std::string restored = snapshot.metadata.title;
    if (restored.empty()) {
        restored = snapshot.metadata.id;
    }
    append(TuiLogKind::System, "Session restored", std::move(restored));

    for (const auto& message : snapshot.messages) {
        switch (message.kind) {
        case agent::SessionMessageKind::System:
            break;
        case agent::SessionMessageKind::UserPrompt:
            ++task_count_;
            append(
                TuiLogKind::Task,
                "Task " + std::to_string(task_count_),
                message.content);
            break;
        case agent::SessionMessageKind::Assistant:
            append(TuiLogKind::Assistant, "Assistant", message.content);
            break;
        case agent::SessionMessageKind::Observation:
            append(TuiLogKind::Observation, "Observation", message.content);
            break;
        case agent::SessionMessageKind::HostHint:
            append(TuiLogKind::System, "Host", message.content);
            break;
        }
    }
}

void TuiState::append_notice(
    std::string heading,
    std::string content,
    bool error) {
    append(
        error ? TuiLogKind::Error : TuiLogKind::System,
        std::move(heading),
        std::move(content));
}

bool TuiState::running() const noexcept {
    return status_ == TuiStatus::Running || status_ == TuiStatus::Stopping;
}

std::size_t TuiState::step() const noexcept {
    return step_;
}

TuiStatus TuiState::status() const noexcept {
    return status_;
}

std::size_t TuiState::log_revision() const noexcept {
    return log_revision_;
}

std::size_t TuiState::task_id() const noexcept {
    return task_count_;
}

TuiState::TimePoint TuiState::turn_started_at() const noexcept {
    return turn_started_at_;
}

TuiState::TimePoint TuiState::activity_started_at() const noexcept {
    return activity_started_at_;
}

bool TuiState::awaiting_command_approval() const noexcept {
    return !pending_command_.empty();
}

const std::string& TuiState::pending_command() const noexcept {
    return pending_command_;
}

const std::string& TuiState::model_name() const noexcept {
    return model_name_;
}

const std::vector<TuiLogEntry>& TuiState::logs() const noexcept {
    return logs_;
}

std::string TuiState::status_text() const {
    switch (status_) {
    case TuiStatus::Ready:
        return "Ready";
    case TuiStatus::Running:
        switch (activity_) {
        case TuiActivity::Thinking:
            return "Thinking";
        case TuiActivity::AwaitingApproval:
            return "Awaiting approval";
        case TuiActivity::RunningCommand:
            return "Running command";
        case TuiActivity::Idle:
            return "Running";
        }
        return "Running";
    case TuiStatus::Stopping:
        return "Stopping";
    case TuiStatus::Stopped:
        return "Stopped";
    case TuiStatus::StepLimitReached:
        return "Step limit reached";
    case TuiStatus::EmptyResponse:
        return "Empty response";
    case TuiStatus::Error:
        return "Error";
    }
    return "Unknown";
}

std::string TuiState::activity_text() const {
    if (status_ == TuiStatus::Stopping) {
        return "Stopping";
    }
    if (status_ != TuiStatus::Running) {
        return status_text();
    }

    switch (activity_) {
    case TuiActivity::Thinking:
        return "Thinking";
    case TuiActivity::AwaitingApproval:
        if (activity_detail_.empty()) {
            return "Awaiting approval";
        }
        return "Approve " + activity_detail_;
    case TuiActivity::RunningCommand: {
        if (activity_detail_.empty()) {
            return "Running command";
        }
        const std::size_t first_line_end = activity_detail_.find_first_of("\r\n");
        return "Run " + activity_detail_.substr(0, first_line_end);
    }
    case TuiActivity::Idle:
        return "Running";
    }
    return "Running";
}

void TuiState::append(
    TuiLogKind kind,
    std::string heading,
    std::string content) {
    logs_.push_back(TuiLogEntry{
        .kind = kind,
        .heading = std::move(heading),
        .content = std::move(content),
    });
    // revision 同时也是日志条目数，供 TuiSession 计算增量区间。
    ++log_revision_;
}

}  // namespace swe_agent::tui
