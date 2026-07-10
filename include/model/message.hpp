#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace swe_agent::model {

enum class Role { System, User, Assistant, Tool };

struct Message {
    Role role;
    std::string content;
};

using MSG = std::vector<Message>;

}  // namespace swe_agent::model
