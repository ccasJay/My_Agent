#pragma once

#include <string>
#include <unordered_map>

namespace swe_agent::config {

using EnvMap = std::unordered_map<std::string, std::string>;

EnvMap load_env(const std::string& path);
std::string get_required(const EnvMap& env, const std::string& key);

}  // namespace swe_agent::config
