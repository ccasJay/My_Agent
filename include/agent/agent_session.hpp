#pragma once

#include "agent/agent_event.hpp"
#include "agent/agent_run_result.hpp"
#include "config/agent_loader.hpp"
#include "model/message.hpp"
#include "model/model.hpp"

namespace swe_agent::agent {

// 核心会话骨架：持有模型客户端与 agent 配置，供后续挂 Loop / 审批策略。
// UI 线程模型仍由 tui::TuiSession 负责。
class AgentSession {
public:
    AgentSession(
        model::ModelClient& client,
        config::AgentConfig config);
    
    AgentRunResult submit(
        std::string user_message,
        const AgentRunOptions& options = {});
    
    void clear();

    [[nodiscard]]
    const model::MSG& history() const  noexcept;

private:
    model::ModelClient& client_;
    config::AgentConfig config_;
    model::MSG history_;
};

}  // namespace swe_agent::agent
