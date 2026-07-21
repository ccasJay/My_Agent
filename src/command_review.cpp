#include "app_cli/command_review.hpp"

#include <algorithm>
#include <cctype>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>

namespace swe_agent::app_cli {
namespace {

std::string normalize_answer(std::string answer) {
    const auto first = std::find_if_not(answer.begin(), answer.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(answer.rbegin(), answer.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (first >= last) {
        return {};
    }
    answer = std::string{first, last};
    std::transform(answer.begin(), answer.end(), answer.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return answer;
}

}  // namespace

agent::CommandDecision review_console_command(
    const agent::CommandRequest& request,
    std::istream& input,
    std::ostream& output,
    bool interactive) {
    if (!interactive) {
        return {
            .action = agent::CommandAction::Reject,
            .rule_id = "console_non_interactive",
            .reason =
                "Console is not an interactive terminal; human review is unavailable.",
        };
    }

    while (true) {
        output << "Command requires human review:\n$ " << request.command
               << "\nApprove execution? [y/yes/n/no]: " << std::flush;
        std::string answer;
        if (!std::getline(input, answer)) {
            return {
                .action = agent::CommandAction::Reject,
                .rule_id = "console_eof",
                .reason =
                    "Interactive review input ended; the command was not approved.",
            };
        }

        answer = normalize_answer(std::move(answer));
        if (answer == "y" || answer == "yes") {
            return {
                .action = agent::CommandAction::Approve,
            };
        }
        if (answer == "n" || answer == "no") {
            return {
                .action = agent::CommandAction::Reject,
                .rule_id = "user_rejected",
                .reason = "The user rejected this command.",
            };
        }
        output << "Please enter y, yes, n, or no.\n";
    }
}

}  // namespace swe_agent::app_cli
