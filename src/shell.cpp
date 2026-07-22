#include "agent/shell.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

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

bool set_close_on_exec(int descriptor) {
    const int flags = ::fcntl(descriptor, F_GETFD);
    return flags >= 0 &&
        ::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) == 0;
}

void signal_process_group(pid_t child, int signal_number) {
    if (::kill(-child, signal_number) != 0 && errno == ESRCH) {
        (void)::kill(child, signal_number);
    }
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
    std::error_code error;
    const std::filesystem::path working_directory =
        std::filesystem::current_path(error);
    if (error) {
        ProcessResult result;
        result.termination = TerminationKind::ExecutionError;
        result.error_message =
            "[shell] unable to determine the current working directory";
        return result;
    }
    return run_shell(command, working_directory);
}

ProcessResult run_shell(
    const std::string& command,
    const std::filesystem::path& working_directory) {
    return run_shell(command, working_directory, {});
}

ProcessResult run_shell(
    const std::string& command,
    const std::filesystem::path& working_directory,
    StopToken stop_token) {
    ProcessResult result;

    if (command.empty()) {
        result.termination = TerminationKind::ExecutionError;
        result.error_message = "[shell] empty command";
        return result;
    }

    if (stop_token.stop_requested()) {
        result.termination = TerminationKind::ExecutionError;
        result.error_message = "[shell] cancelled before execution";
        return result;
    }

    int pipe_fds[2];
    if (::pipe(pipe_fds) != 0) {
        result.termination = TerminationKind::ExecutionError;
        result.error_message = "[shell] pipe failed: " +
            std::string{std::strerror(errno)};
        return result;
    }
    if (!set_close_on_exec(pipe_fds[0]) ||
        !set_close_on_exec(pipe_fds[1])) {
        const int saved_errno = errno;
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        result.termination = TerminationKind::ExecutionError;
        result.error_message = "[shell] fcntl failed: " +
            std::string{std::strerror(saved_errno)};
        return result;
    }

    const int null_input = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (null_input < 0) {
        const int saved_errno = errno;
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        result.termination = TerminationKind::ExecutionError;
        result.error_message = "[shell] open /dev/null failed: " +
            std::string{std::strerror(saved_errno)};
        return result;
    }
    long maximum_descriptor = ::sysconf(_SC_OPEN_MAX);
    if (maximum_descriptor < 0) {
        maximum_descriptor = 1024;
    }

    const pid_t child = ::fork();
    if (child < 0) {
        const int saved_errno = errno;
        ::close(null_input);
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        result.termination = TerminationKind::ExecutionError;
        result.error_message = "[shell] fork failed: " +
            std::string{std::strerror(saved_errno)};
        return result;
    }

    if (child == 0) {
        (void)::setpgid(0, 0);
        ::close(pipe_fds[0]);
        if (::dup2(null_input, STDIN_FILENO) < 0 ||
            ::dup2(pipe_fds[1], STDOUT_FILENO) < 0 ||
            ::dup2(pipe_fds[1], STDERR_FILENO) < 0) {
            ::_exit(127);
        }
        for (int descriptor = STDERR_FILENO + 1;
             descriptor < maximum_descriptor;
             ++descriptor) {
            ::close(descriptor);
        }

        if (::chdir(working_directory.c_str()) != 0) {
            constexpr char kMessage[] = "[shell] chdir failed\n";
            (void)::write(STDERR_FILENO, kMessage, sizeof(kMessage) - 1);
            ::_exit(126);
        }
        ::execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        constexpr char kMessage[] = "[shell] exec failed\n";
        (void)::write(STDERR_FILENO, kMessage, sizeof(kMessage) - 1);
        ::_exit(127);
    }

    ::close(null_input);
    ::close(pipe_fds[1]);
    if (::setpgid(child, child) != 0 && errno != EACCES && errno != ESRCH) {
        result.error_message = "[shell] setpgid failed: " +
            std::string{std::strerror(errno)};
    }
    std::array<char, 512> buf{};
    constexpr std::size_t kMaxBytes = 16 * 1024;
    constexpr auto kPollInterval = std::chrono::milliseconds{50};
    constexpr auto kTerminationGrace = std::chrono::seconds{1};
    bool termination_requested = false;
    bool force_killed = false;
    std::chrono::steady_clock::time_point force_kill_at{};
    while (true) {
        if (stop_token.stop_requested() && !termination_requested) {
            signal_process_group(child, SIGTERM);
            termination_requested = true;
            force_kill_at = std::chrono::steady_clock::now() +
                kTerminationGrace;
        }
        if (termination_requested && !force_killed &&
            std::chrono::steady_clock::now() >= force_kill_at) {
            signal_process_group(child, SIGKILL);
            force_killed = true;
        }

        pollfd descriptor{
            .fd = pipe_fds[0],
            .events = POLLIN | POLLHUP,
            .revents = 0,
        };
        const int poll_result = ::poll(
            &descriptor,
            1,
            static_cast<int>(kPollInterval.count()));
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            result.error_message = "[shell] poll failed: " +
                std::string{std::strerror(errno)};
            signal_process_group(child, SIGKILL);
            break;
        }
        if (poll_result == 0) {
            continue;
        }

        const ssize_t count = ::read(pipe_fds[0], buf.data(), buf.size());
        if (count == 0) {
            break;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            result.error_message = "[shell] read failed: " +
                std::string{std::strerror(errno)};
            signal_process_group(child, SIGKILL);
            break;
        }
        if (result.truncated) {
            // 仍需排空管道，否则高输出子进程可能阻塞在 write。
            continue;
        }
        result.output.append(buf.data(), static_cast<std::size_t>(count));
        if (result.output.size() > kMaxBytes) {
            result.output.resize(kMaxBytes);
            result.truncated = true;
        }
    }
    ::close(pipe_fds[0]);

    int status = 0;
    pid_t waited = 0;
    do {
        waited = ::waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);

    if (waited < 0) {
        result.termination = TerminationKind::ExecutionError;
        result.error_message = "[shell] waitpid failed: " +
            std::string{std::strerror(errno)};
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
