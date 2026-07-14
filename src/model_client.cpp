#include "http/http_client.hpp"
#include "model/message.hpp"
#include "model/model_client.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

namespace  {
using Role = swe_agent::model::Role;

/**
 * @brief Role 枚举转字符串
 * 
 * @param role 
 * @return const char* 
 */
const char* role_to_string(Role role) {
    switch (role) {
        case Role::System: return "system";
        case Role::User: return "user";
        case Role::Assistant: return "assistant";
        case Role::Tool: return "tool";
    }
    throw std::invalid_argument{"unknown Role"};
}
} //namespace

namespace swe_agent::model {

/**
 * @brief 符合Provider契约的 OpenAI 兼容 API 实现
 * 
 * @note 先组装api_messages -> 构建chat body -> 构建请求头 -> 调用 HTTP POST 接口 (http::HttpClient::post())
 * @param messages 
 * @return ModelResponse 
 */
ModelResponse OpenaiCompatible::query(const MSG& messages) {
    if (messages.empty()) {
        return ModelResponse{"Messages are empty"};
    }

    // 配置由 main: load_env → ModelConfig 注入；query 只负责发请求
    nlohmann::json api_messages = nlohmann::json::array();
    for (const auto& msg : messages) {
        api_messages.push_back({
            {"role",role_to_string(msg.role)},
            {"content",msg.content},
        });
        (void)msg;
    }

    nlohmann::json body = {
        {"model", config_.model_name},
        {"messages", api_messages},
    };

    const std::vector<std::string> headers{
        "Content-Type: application/json",
        "Authorization: Bearer " + config_.api_key,
    };

    http::HttpClient http_client;
    /* 调用统一的 HTTP POST 接口 */
    const http::HttpResponse response = http_client.post(
        config_.base_url,
        headers,
        body.dump()
    );

    if (response.status_code < 200 || response.status_code >= 300) {
        throw std::runtime_error{
            "Model API returned HTTP " + std::to_string(response.status_code) +
            ": " + response.body
        };
    }

    /* 使用 nlohmann::json 进行解析 */
    auto result = nlohmann::json::parse(response.body);

    return ModelResponse{result["choices"][0]["message"]["content"]};
}


} // namespace swe_agent::model
