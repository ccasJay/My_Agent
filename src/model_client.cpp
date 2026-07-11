#include "http/http_client.hpp"
#include "model/model_client.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

namespace swe_agent::model {

ModelResponse OpenaiCompatible::query(const MSG& messages) {
    if (messages.empty()) {
        return ModelResponse{"Messages are empty"};
    }

    // 配置由 main: load_env → ModelConfig 注入；query 只负责发请求
    nlohmann::json body = {
        {"model", config_.model_name},
        {"messages", {
            {
                {"role","system"},
                {"content", "hi"}
            }
        }}
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
