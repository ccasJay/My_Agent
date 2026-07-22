#pragma once

#include "model/model.hpp"

namespace swe_agent::model {

// OpenAI 兼容 API 的一种 Provider 实现
class OpenaiCompatible final : public IProvider {
public:
    explicit OpenaiCompatible(const ModelConfig& config) : config_(config) {}

    ModelResponse query(const MSG& messages) override;
    ModelResponse query(
        const MSG& messages,
        agent::StopToken stop_token) override;

private:
    ModelConfig config_;
};

static_assert(Provider<OpenaiCompatible>,
              "OpenaiCompatible does not satisfy Provider");

}  // namespace swe_agent::model
