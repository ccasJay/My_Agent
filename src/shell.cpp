#include "agent/shell.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <sys/wait.h>

namespace swe_agent::agent {
namespace {

/**
 * @brief 去掉字符串右侧的换行符（\n 或 \r），用于 shell 输出处理
 *
 * @param s
 * @return std::string
 */
std::string trim_right_newlines(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

}  // namespace

/**
 * @brief 从 assistant 文本里解析要执行的命令。
 *
 * @param assistant_text
 * @return std::optional<std::string>
 */
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
        while (j < cmd.size() && (cmd[j] == ' ' || cmd[j] == '\t')) {
            ++j;
        }
        // 必须 substr(j)，否则 RUN: 后空格会留在命令里（日志出现 "$  ls"）
        cmd = trim_right_newlines(cmd.substr(j));
        if (!cmd.empty()) {
            return cmd;
        }
    }
    return std::nullopt;
}

/**
 * @brief 执行 shell 命令，合并 stdout/stderr，并返回结构化结果（不抛）。
 *
 * @param command
 * @return ProcessResult
 */
ProcessResult run_shell(const std::string& command) {
    ProcessResult result;

    if (command.empty()) {
        result.termination = TerminationKind::ExecutionError;
        result.error_message = "[shell] empty command";
        return result;
    }

    // stderr 并入 stdout，便于整段作为 observation
    const std::string full = command + " 2>&1";
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(full.c_str(), "r"), pclose);
    if (!pipe) {
        result.termination = TerminationKind::ExecutionError;
        result.error_message = "[shell] popen failed for: " + command;
        return result;
    }

    std::array<char, 512> buf{};
    constexpr std::size_t kMaxBytes = 16 * 1024;
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe.get()) != nullptr) {
        if (result.truncated) {
            // 仍需排空管道，否则高输出子进程可能阻塞在 write，pclose 也会一直等待。
            continue;
        }
        result.output.append(buf.data());
        if (result.output.size() > kMaxBytes) {
            result.output.resize(kMaxBytes);
            result.truncated = true;
        }
    }

    // 需要 exit status：先 release 再 pclose（否则 unique_ptr 析构也会 pclose）
    FILE* raw = pipe.release();
    const int status = pclose(raw);
    if (status == -1) {
        result.termination = TerminationKind::ExecutionError;
        result.error_message = "[shell] pclose failed for: " + command;
    } else if (WIFEXITED(status)) {
        result.termination = TerminationKind::Exited;
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.termination = TerminationKind::Signaled;
        result.signal_number = WTERMSIG(status);
    } else {
        result.termination = TerminationKind::Unknown;
    }

    return result;
}

/**
 * @brief 将结构化执行结果转换成给 Agent/人阅读的 observation。
 *
 * @param command
 * @param result
 * @return std::string
 */
std::string format_process_result(
    const std::string& command,
    const ProcessResult& result) {
    std::ostringstream oss;
    oss << "$ " << command << '\n';
    if (result.output.empty()) {
        oss << "(no output)\n";
    } else {
        oss << result.output;
        if (result.output.back() != '\n') {
            oss << '\n';
        }
    }
    if (result.truncated) {
        oss << "...[truncated]\n";
    }
    if (!result.error_message.empty()) {
        oss << result.error_message << '\n';
    } else if (result.termination == TerminationKind::Exited &&
               result.exit_code.has_value() && *result.exit_code != 0) {
        oss << "[exit=" << *result.exit_code << "]\n";
    } else if (result.termination == TerminationKind::Signaled &&
               result.signal_number.has_value()) {
        oss << "[signal=" << *result.signal_number << "]\n";
    } else if (result.termination == TerminationKind::Unknown) {
        oss << "[status=unknown]\n";
    }
    return oss.str();
}

}  // namespace swe_agent::agent
