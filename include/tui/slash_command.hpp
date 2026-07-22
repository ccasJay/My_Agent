#pragma once

#include "tui/slash_types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace swe_agent::tui {

struct SlashContext;

/**
 * @brief TUI 斜杠元命令接口。
 *
 * 实现不得依赖 FTXUI；仅通过 SlashContext 访问 Session 与 UI 通知。
 * 调用约定：仅在 UI 线程执行。
 */
class SlashCommand {
public:
    virtual ~SlashCommand() = default;

    /** @brief 规范名，不含前导 '/'（例如 "new"）。 */
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    /** @brief 可选别名；v1 返回空。 */
    [[nodiscard]] virtual std::vector<std::string_view> aliases() const {
        return {};
    }

    /** @brief 一行帮助摘要（英文 UI 文案）。 */
    [[nodiscard]] virtual std::string_view summary() const noexcept = 0;

    /** @brief 用法字符串（例如 "/resume <session-id-prefix>"）。 */
    [[nodiscard]] virtual std::string_view usage() const noexcept = 0;

    /**
     * @brief 参数校验。
     * @return nullopt 表示通过；否则为错误消息（常为 "Usage: ..."）。
     */
    [[nodiscard]] virtual std::optional<std::string> validate(
        std::string_view args) const {
        (void)args;
        return std::nullopt;
    }

    /** @brief 在 UI 线程执行命令副作用。 */
    virtual void execute(SlashContext& ctx, std::string_view args) const = 0;
};

}  // namespace swe_agent::tui
