#pragma once

#include <filesystem>
#include <string>
#include <string_view>
namespace swe_agent::agent {

/** @brief 命令策略的分类结果。 */
enum class PolicyAction {
    Allow,
    RequireReview,
    Deny,
};

/** @brief 执行命令时供策略判断使用的工作区上下文。 */
struct PolicyContext {
    std::filesystem::path working_dir;
    std::filesystem::path workspace_root;
};

/** @brief 命令策略的判定结果及可展示原因。 */
struct PolicyResult {
    PolicyAction action;
    std::string rule_id;
    std::string reason;
};

/**
 * @brief 对命令文本进行保守的非交互式策略分类。
 *
 * 该函数不执行命令，也不解析完整的 POSIX Shell；无法安全判断的输入会要求人工审核。
 *
 * @param command 模型请求执行的命令文本。
 * @param context 当前工作目录及工作区根目录。
 * @return 包含策略动作、规则标识和中文原因的判定结果。
 */
PolicyResult evaluate_command_policy(
    std::string_view command,
    const PolicyContext& context
);

}
