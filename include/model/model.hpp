#pragma once

#include "agent/cancellation.hpp"
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

    /** @brief 查询模型；默认兼容不支持主动取消的 Provider。 */
    virtual ModelResponse query(
        const MSG& messages,
        agent::StopToken stop_token) {
        (void)stop_token;
        return query(messages);
    }
};

struct ModelConfig {
    std::string base_url;
    std::string api_key;
    std::string model_name;
};
struct ModelClient : public IProvider {
    explicit ModelClient(const ModelConfig& config);
    ModelResponse query(const MSG& messages) override;
    ModelResponse query(
        const MSG& messages,
        agent::StopToken stop_token) override;
private:
    std::unique_ptr<IProvider> provider_;
};


// 模型后端契约：任意能 query(MSG) → ModelResponse 的类型
template <typename P>
concept Provider = requires(P& provider, const MSG& messages) {
    { provider.query(messages) } -> std::same_as<ModelResponse>;
};

/** @brief 优先调用支持 StopToken 的 Provider 查询接口。 */
template <Provider P>
ModelResponse query_provider(
    P& provider,
    const MSG& messages,
    agent::StopToken stop_token) {
    if constexpr (requires {
        { provider.query(messages, stop_token) } -> std::same_as<ModelResponse>;
    }) {
        return provider.query(messages, stop_token);
    }
    return provider.query(messages);
}

}  // namespace swe_agent::model
