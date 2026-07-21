#pragma once

#include <filesystem>

namespace swe_agent::agent {

/**
 * @brief 默认会话 SQLite 路径（通常在用户配置/数据目录下）
 * @return 绝对或规范路径，供 SqliteSessionStore 使用
 */
[[nodiscard]] std::filesystem::path session_database_path();

}  // namespace swe_agent::agent
