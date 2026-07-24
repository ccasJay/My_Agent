#pragma once

#include "tui/slash_command.hpp"
#include "tui/slash_types.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace swe_agent::tui {

struct SlashContext;

/**
 * @brief 进程内斜杠命令注册表。
 *
 * 非线程安全：仅在 UI 线程构造、注册与 dispatch。
 */
class SlashRegistry {
public:
    /**
     * @brief 注册命令并取得所有权。
     * @throws std::invalid_argument 名称或别名冲突。
     */
    void register_command(std::unique_ptr<SlashCommand> command);

    [[nodiscard]] const SlashCommand* find(std::string_view name) const noexcept;

    /** @brief 按注册顺序返回规范命令（不含别名条目）。 */
    [[nodiscard]] std::vector<const SlashCommand*> list() const;

    [[nodiscard]] SlashParseResult parse(std::string_view line) const;

    /**
     * @brief 解析并执行；UsageError/Unknown 发 notice；execute 异常发 Session error。
     * @return NotACommand 时 host 应提交普通任务；否则已消费输入。
     */
    [[nodiscard]] SlashDispatchStatus dispatch(
        SlashContext& ctx,
        std::string_view line) const;

private:
    std::vector<std::unique_ptr<SlashCommand>> commands_;
    std::unordered_map<std::string, SlashCommand*> by_name_;
};

/** @brief 注册内置 /new /sessions /resume /help。 */
void register_builtin_slash_commands(SlashRegistry& registry);

}  // namespace swe_agent::tui
