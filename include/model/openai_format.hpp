#pragma once

#include "model/message.hpp"
#include "model/model.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace swe_agent::model {

/**
 * @brief 将 Role 枚举映射为 OpenAI chat-completions 的 role 字符串
 */
inline const char* role_to_string(Role role) {
    switch (role) {
        case Role::System:
            return "system";
        case Role::User:
            return "user";
        case Role::Assistant:
            return "assistant";
        case Role::Tool:
            return "tool";
    }
    throw std::invalid_argument{"unknown Role"};
}

/**
 * @brief 构造 OpenAI 兼容的 chat completion 请求体（纯函数，不访问网络）
 *
 * 形状：
 * {
 *   "model": "<model_name>",
 *   "messages": [ {"role": "...", "content": "..."}, ... ]
 * }
 */
inline nlohmann::json build_request_body(const ModelConfig& config,
                                         const MSG& messages) {
    nlohmann::json api_messages = nlohmann::json::array();
    for (const auto& msg : messages) {
        api_messages.push_back({
            {"role", role_to_string(msg.role)},
            {"content", msg.content},
        });
    }

    return nlohmann::json{
        {"model", config.model_name},
        {"messages", api_messages},
    };
}

}  // namespace swe_agent::model
