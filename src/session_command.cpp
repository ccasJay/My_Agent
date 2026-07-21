#include "tui/session_command.hpp"

#include <cctype>

namespace swe_agent::tui {
namespace {

std::string_view trim(std::string_view value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

}  // namespace

SessionCommand parse_session_command(std::string_view input) {
    input = trim(input);
    if (input.empty() || input.front() != '/') {
        return {};
    }
    if (input == "/new") {
        return {.kind = SessionCommandKind::New};
    }
    if (input == "/sessions") {
        return {.kind = SessionCommandKind::List};
    }
    if (input.starts_with("/resume") &&
        (input.size() == 7 ||
         std::isspace(static_cast<unsigned char>(input[7])) != 0)) {
        std::string_view argument = trim(input.substr(7));
        if (argument.empty()) {
            return {
                .kind = SessionCommandKind::Invalid,
                .error = "Usage: /resume <session-id-prefix>",
            };
        }
        if (argument.find_first_of(" \t\r\n") != std::string_view::npos) {
            return {
                .kind = SessionCommandKind::Invalid,
                .error = "Session id prefix cannot contain whitespace",
            };
        }
        return {
            .kind = SessionCommandKind::Resume,
            .argument = std::string{argument},
        };
    }
    return {
        .kind = SessionCommandKind::Invalid,
        .error = "Unknown command: " + std::string{input},
    };
}

}  // namespace swe_agent::tui
