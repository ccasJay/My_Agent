#pragma once

#include <optional>
#include <string>

namespace swe_agent::agent {

enum class TerminationKind {
    Exited,
    Signaled,
    ExecutionError,
    Unknown,
};

struct ProcessResult {
    std::string output;
    std::string error_message;

    TerminationKind termination{TerminationKind::Unknown};

    // 仅当 termination == Exited 时有值。
    std::optional<int> exit_code;

    // 仅当 termination == Signaled 时有值。
    std::optional<int> signal_number;

    bool truncated{false};

    // 正常退出且 exit_code 为 0 时才算成功。
    [[nodiscard]] bool success() const noexcept;
};

}  // namespace swe_agent::agent
