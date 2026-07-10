#pragma once

#include "model/message.hpp"
#include <concepts>
#include <string>

namespace swe_agent::model {

struct ModelResponse {
    std::string content;
};

struct ModelConfig {
    std::string base_url;
    std::string api_key;
    std::string model_name;
};

template <typename MODEL>
concept Model = requires(
    MODEL& client,
    const MSG& messages,
    struct ModelConfig config
) {
    {
        client.query(messages)
    } -> std::same_as<ModelResponse>;

};

}  // namespace swe_agent::model
