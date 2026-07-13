#include "config/dotenv_loader.hpp"

#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>

namespace swe_agent::config {

/**
 * @brief 去掉两端空白：空格/Tab/\r/\n 等（std::isspace）
 * 
 * @param str 
 * @return std::string 
 */
std::string trim(const std::string& str) {
    std::size_t begin = 0;
    while (begin < str.size() &&
           std::isspace(static_cast<unsigned char>(str[begin]))) {
        ++begin;
    }

    std::size_t end = str.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(str[end - 1]))) {
        --end;
    }
    return str.substr(begin, end - begin);
}

/**
 * @brief 加载环境变量
 * 
 * @param path 
 * @return EnvMap 
 */
EnvMap load_env(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error{"Cannot open env file: " + path};
    }

    EnvMap env;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);

        // 空行或注释
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;  // 无 '='，忽略；也可改成 throw
        }

        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        // 可选：去掉一对引号 "..." 或 '...'
        if (value.size() >= 2) {
            const char q = value.front();
            if ((q == '"' || q == '\'') && value.back() == q) {
                value = value.substr(1, value.size() - 2);
            }
        }

        if (key.empty()) {
            continue;
        }

        env[key] = value;
    }
    return env;
}

/**
 * @brief 获取环境变量的对应值
 * 
 * @param env 
 * @param key 
 * @return std::string 
 */
std::string get_required(const EnvMap& env, const std::string& key) {
    const auto it = env.find(key);
    if (it == env.end() || it->second.empty()) {
        throw std::runtime_error{"Missing required config: " + key};
    }
    return it->second;
}

}  // namespace swe_agent::config
