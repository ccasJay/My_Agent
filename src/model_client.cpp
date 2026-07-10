#include "model/model_client.hpp"
#include <curl/curl.h>
#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

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

size_t WriteCallback (
    void* contents,
    size_t size ,//实际数据长度： size * nmemb
    size_t nmemb ,
    void* userp 
) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
};

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

    CURL* curl = curl_easy_init(); // 初始化一个CURL对象

    std::string response;

    nlohmann::json body = {
        {"model", config_.model_name},
        {"messages", {
            {
                {"role","system"},
                {"content", "hi"}
            }
        }}
    };

    struct curl_slist* headers = nullptr;

    /* 设置 HTTP Header */
    headers = curl_slist_append(
        headers,
        "Content-Type: application/json"
    );
    
    headers = curl_slist_append(
            headers,
            ("Authorization: Bearer " + config_.api_key).c_str()
    );

    /* 设置请求 URL*/
    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        config_.base_url.c_str()
    );

    /* json对象转换为字符串 */
    std::string data = body.dump();

    /* 设置 HTTP POST 请求体 */
    curl_easy_setopt(
        curl,
        CURLOPT_POSTFIELDS,
        data.c_str()
    );

    /* 设置 HTTP Header */
    curl_easy_setopt (
        curl,
        CURLOPT_HTTPHEADER,
        headers
    );

    /* 设置响应处理函数 */
    curl_easy_setopt (
        curl,
        CURLOPT_WRITEFUNCTION,
        WriteCallback
    );

    /* 传入WriteCallback 的用户数据 */
    curl_easy_setopt (
        curl,
        CURLOPT_WRITEDATA,
        &response
    );

    /* 执行 HTTP 请求 */
    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        std::cerr 
            << "curl error: "
            << curl_easy_strerror(res)
            << std::endl;
        curl_easy_cleanup(curl);
        return ModelResponse{"curl error: " + std::string(curl_easy_strerror(res))};
    }

    /* 使用 nlohmann::json 进行解析 */
    auto result = nlohmann::json::parse(response);

    std::cout
        << result["choices"][0]["message"]["content"]
        << std::endl;


    /* free libcurl resourse */
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return ModelResponse{result["choices"][0]["message"]["content"]};
}


} // namespace swe_agent::model
