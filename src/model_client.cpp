#include "http/http_client.hpp"
#include "model/message.hpp"
#include "model/model_client.hpp"
#include "model/model.hpp"
#include "model/openai_format.hpp"

#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

namespace swe_agent::model {

ModelClient::ModelClient(const ModelConfig& config) : provider_(std::make_unique<OpenaiCompatible>(config)) {}

ModelResponse ModelClient::query(const MSG& message) {
    return provider_->query(message);
}

/**
 * @brief 符合 Provider 契约的 OpenAI 兼容 API 实现
 *
 * @note 组装 body → 请求头 → HTTP POST；body 构建见 openai_format.hpp
 * @param messages 对话消息
 * @return ModelResponse 模型文本回复
 *
 * @note 全路径依赖 HttpClient/curl，单元测试不覆盖真实网络调用；
 *       请求 JSON 形状由 build_request_body / role_to_string 单独测。
 */
ModelResponse OpenaiCompatible::query(const MSG& messages) {
    if (messages.empty()) {
        return ModelResponse{"Messages are empty"};
    }

    const nlohmann::json body = build_request_body(config_, messages);

    const std::vector<std::string> headers{
        "Content-Type: application/json",
        "Authorization: Bearer " + config_.api_key,
    };

    http::HttpClient http_client;
    const http::HttpResponse response =
        http_client.post(config_.base_url, headers, body.dump());

    if (response.status_code < 200 || response.status_code >= 300) {
        throw std::runtime_error{
            "Model API returned HTTP " + std::to_string(response.status_code) +
            ": " + response.body};
    }

    const auto result = nlohmann::json::parse(response.body);
    return ModelResponse{result["choices"][0]["message"]["content"]};
}

}  // namespace swe_agent::model
