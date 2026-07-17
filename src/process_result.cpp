#include "agent/process_result.hpp"

namespace swe_agent::agent {

bool ProcessResult::success() const noexcept {
    return termination == TerminationKind::Exited &&
           exit_code.has_value() &&
           *exit_code == 0;
}

}  // namespace swe_agent::agent
