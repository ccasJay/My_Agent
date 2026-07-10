#pragma once

#include "model/model.hpp"

namespace swe_agent::model {
class OpenaiCompatible {
public:
    explicit OpenaiCompatible(const ModelConfig& config) : config_(config) {};

    ModelResponse query(const MSG& messages);
private:
    ModelConfig config_;
};

static_assert(Model<OpenaiCompatible>, "OpenaiCompatible does not satisfy the Model concept");

} // namespace swe_agent::model

