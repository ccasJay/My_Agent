#include "agent/session_paths.hpp"

#include "agent/session_store.hpp"

#include <cstdlib>
#include <string>

namespace swe_agent::agent {

std::filesystem::path session_database_path() {
    if (const char* override_dir = std::getenv("SWE_AGENT_DATA_DIR");
        override_dir != nullptr && override_dir[0] != '\0') {
        return std::filesystem::path{override_dir} / "agent.db";
    }

    const char* user_home = std::getenv("HOME");
    if (user_home == nullptr || user_home[0] == '\0') {
        throw SessionStorageError{
            "Cannot determine session data directory: HOME is not set"};
    }

#if defined(__APPLE__)
    return std::filesystem::path{user_home} /
        "Library" / "Application Support" / "swe-agent" / "agent.db";
#else
    if (const char* xdg_data_home = std::getenv("XDG_DATA_HOME");
        xdg_data_home != nullptr && xdg_data_home[0] != '\0') {
        return std::filesystem::path{xdg_data_home} /
            "swe-agent" / "agent.db";
    }
    return std::filesystem::path{user_home} /
        ".local" / "share" / "swe-agent" / "agent.db";
#endif
}

}  // namespace swe_agent::agent
