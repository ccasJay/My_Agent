#include "http/http_client.hpp"
#include "model/model_client.hpp"
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

namespace {

std::string get_required_env(const std::string& name) {
    const char* value = std::getenv(name.c_str());
    if (value == nullptr) {
        throw std::runtime_error{"Missing environment variable: " + name};
    }
    return std::string{value};
}

}  // namespace

namespace swe_agent::model {

ModelResponse OpenaiCompatible::query(const MSG& messages) {
    if (messages.empty()) {
        return ModelResponse{"Messages are empty"};
    }

    if (config_.base_url.empty()) {
        config_.base_url = get_required_env("OPENAI_BASE_URL");
    }
    if (config_.api_key.empty()) {
        config_.api_key = get_required_env("OPENAI_API_KEY");
    }
    if (config_.model_name.empty()) {
        config_.model_name = get_required_env("OPENAI_MODEL");
    }

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
