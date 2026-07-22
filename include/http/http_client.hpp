#pragma once

#include "agent/cancellation.hpp"

#include <string>
#include <vector>

namespace swe_agent::http {

struct HttpResponse {
    long status_code{};
    std::string body;
};

class HttpClient {
public:
    HttpResponse post(
        const std::string& url,
        const std::vector<std::string>& headers,
        const std::string& body
    ) const;

    /** @brief 执行可由 StopToken 协作取消的 HTTP POST。 */
    HttpResponse post(
        const std::string& url,
        const std::vector<std::string>& headers,
        const std::string& body,
        agent::StopToken stop_token) const;
};

}  // namespace swe_agent::http
