#include "agent/shell.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>

namespace swe_agent::agent {
namespace {

std::string trim_right_newlines(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

}  // namespace

std::optional<std::string> extract_run_command(const std::string& assistant_text) {
    std::istringstream in (assistant_text);
    std::string line;

    while (std::getline(in, line)) {
        std::size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }

        constexpr const char* kPrefix = "RUN:";
        constexpr std::size_t kPrefixlen = 4;

        if(line.compare(i, kPrefixlen, kPrefix) != 0) {
            continue;
        }

        std::string cmd = line.substr(i + kPrefixlen);
        std::size_t j = 0;
        while (j<cmd.size() && (cmd[j] == ' ' || cmd[j] == '\t')) {
            ++j;
        }
        cmd = trim_right_newlines(std::move(cmd));
        if (!cmd.empty()) {
            return cmd;
        }
    }
    return std::nullopt;
}

std::string run_shell(const std::string& command) {
    if (command.empty()) {
        return "[shell] empty command";
    }

    // stderr 并入 stdout，便于整段作为 observation
    const std::string full = command + " 2>&1";
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(full.c_str(), "r"), pclose);
    if (!pipe) {
        return "[shell] popen failed for: " + command;
    }

    std::string output;
    std::array<char, 512> buf{};
    constexpr std::size_t kMaxBytes = 8 * 1024;
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe.get()) != nullptr) {
        output.append(buf.data());
        if (output.size() > kMaxBytes) {
            output.resize(kMaxBytes);
            output += "\n...[truncated]";
            break;
        }
    }

    // 需要 exit status：先 release 再 pclose（否则 unique_ptr 析构也会 pclose）
    FILE* raw = pipe.release();
    const int status = pclose(raw);

    std::ostringstream oss;
    oss << "$ " << command << '\n';
    if (output.empty()) {
        oss << "(no output)\n";
    } else {
        oss << output;
        if (output.back() != '\n') {
            oss << '\n';
        }
    }
    if (status != 0) {
        oss << "[exit=" << status << "]\n";
    }
    return oss.str();
}

}  // namespace swe_agent::agent
