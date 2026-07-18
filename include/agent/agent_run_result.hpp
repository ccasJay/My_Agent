#pragma once

#include "model/model.hpp"

#include <cstddef>

namespace swe_agent::agent {

// Agent Loop 返回时已经确定的终态，与运行中的 AgentEvent 分离。
enum class AgentRunStatus {
    Completed,
    Stopped,
    StepLimitReached,
    EmptyResponse,
};

struct AgentRunResult {
    AgentRunStatus status;
    // 最后一次模型响应；若在首次 query 前停止，则保持为空。
    model::ModelResponse response;
    // 退出时所在的 step，编号规则与 AgentEvent::step 一致。
    std::size_t step{0};
};

}  // namespace swe_agent::agent
