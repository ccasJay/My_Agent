#pragma once

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
};

}  // namespace swe_agent::http
