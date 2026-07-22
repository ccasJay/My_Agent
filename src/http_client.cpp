#include "http/http_client.hpp"

#include <curl/curl.h>
#include <stdexcept>
#include <string>

namespace {

struct ProgressContext {
    swe_agent::agent::StopToken stop_token;
};

/**
 * @brief libcurl 回调函数，用于接收响应数据
 * 
 * @param contents 
 * @param size 
 * @param nmemb 
 * @param userp 
 * @return std::size_t 
 */
std::size_t WriteCallback (
    char* contents,
    std::size_t size,
    std::size_t nmemb,
    void* userp
) noexcept {
    const std::size_t bytes = size * nmemb;
    auto* response = static_cast<std::string*>(userp);

    try {
        response->append(contents, bytes);
        return bytes;
    } catch (...) {
        return 0;
    }
}

int ProgressCallback(
    void* clientp,
    curl_off_t,
    curl_off_t,
    curl_off_t,
    curl_off_t) noexcept {
    const auto* context = static_cast<const ProgressContext*>(clientp);
    return context != nullptr && context->stop_token.stop_requested() ? 1 : 0;
}

}  // namespace

namespace swe_agent::http {

/**
 * @brief 执行 HTTP POST 请求
 * 
 * @param url 
 * @param headers 
 * @param body 
 * @return HttpResponse 
 */
HttpResponse HttpClient::post(
    const std::string& url,
    const std::vector<std::string>& headers,
    const std::string& body
) const {
    return post(url, headers, body, {});
}

HttpResponse HttpClient::post(
    const std::string& url,
    const std::vector<std::string>& headers,
    const std::string& body,
    agent::StopToken stop_token
) const {
    /* 初始化 CURL 对象 */
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        throw std::runtime_error{"Failed to initialize curl"};
    }

    /* 构造 HTTP Header */
    struct curl_slist* header_list = nullptr;
    for (const auto& header : headers) {
        struct curl_slist* appended = curl_slist_append(
            header_list,
            header.c_str()
        );

        if (appended == nullptr) {
            curl_slist_free_all(header_list);
            curl_easy_cleanup(curl);
            throw std::runtime_error{"Failed to allocate curl headers"};
        }
        header_list = appended;
    }

    std::string response;
    const ProgressContext progress_context{.stop_token = stop_token};

    /* 设置请求 URL */
    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        url.c_str()
    );

    /* 设置 HTTP POST 请求体 */
    curl_easy_setopt(
        curl,
        CURLOPT_POSTFIELDS,
        body.c_str()
    );
    curl_easy_setopt(
        curl,
        CURLOPT_POSTFIELDSIZE_LARGE,
        static_cast<curl_off_t>(body.size())
    );

    /* 设置 HTTP Header */
    curl_easy_setopt(
        curl,
        CURLOPT_HTTPHEADER,
        header_list
    );

    /* 设置响应处理函数 */
    curl_easy_setopt(
        curl,
        CURLOPT_WRITEFUNCTION,
        WriteCallback
    );
    curl_easy_setopt(
        curl,
        CURLOPT_WRITEDATA,
        &response
    );
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_context);

    /* 超时：连接阶段与整次请求上限，避免一直挂起 */
    constexpr long kConnectTimeoutSec = 10;
    constexpr long kTotalTimeoutSec = 60;
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSec);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kTotalTimeoutSec);
    // 避免信号打断多线程环境；用 curl 自身超时即可
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    /* 执行 HTTP 请求 */
    CURLcode result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
        std::string error = curl_easy_strerror(result);
        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);
        if (result == CURLE_ABORTED_BY_CALLBACK &&
            stop_token.stop_requested()) {
            throw agent::OperationCancelled{};
        }
        throw std::runtime_error{"curl request failed: " + error};
    }

    /* 获取 HTTP 状态码 */
    long status_code = 0;
    CURLcode info_result = curl_easy_getinfo(
        curl,
        CURLINFO_RESPONSE_CODE,
        &status_code
    );

    if (info_result != CURLE_OK) {
        std::string error = curl_easy_strerror(info_result);
        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);
        throw std::runtime_error{"Failed to read HTTP status: " + error};
    }

    HttpResponse http_response{status_code, response};

    /* 释放 libcurl 资源 */
    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);

    return http_response;
}

}  // namespace swe_agent::http
