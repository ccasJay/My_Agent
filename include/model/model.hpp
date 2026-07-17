#pragma once

#include "model/message.hpp"
#include <concepts>
#include <string>
#include <memory>

namespace swe_agent::model {

struct ModelResponse {
    std::string content;
};
class IProvider {
public:
    virtual ~IProvider() = default;
    virtual ModelResponse query(const MSG& messages) = 0;
};

struct ModelConfig {
    std::string base_url;
    std::string api_key;
    std::string model_name;
};
struct ModelClient {
    explicit ModelClient(const ModelConfig& config);
    ModelResponse query(const MSG& messages); 
private:
    std::unique_ptr<IProvider> provider_;
};


// 模型后端契约：任意能 query(MSG) → ModelResponse 的类型
template <typename P>
concept Provider = requires(P& provider, const MSG& messages) {
    { provider.query(messages) } -> std::same_as<ModelResponse>;
};

}  // namespace swe_agent::model
