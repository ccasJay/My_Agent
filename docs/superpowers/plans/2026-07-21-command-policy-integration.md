# Command Policy Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 TUI 与 Console 提供统一、不可绕过的命令策略授权，并将拒绝结果一致地反馈给用户和模型。

**Architecture:** `command_policy` 只负责保守分类；新增 `command_authorization` 将策略结果映射为 `CommandDecision`；TUI 与 Console 分别提供审核回调。Agent Loop 对所有拒绝发出结构化事件并写入 Observation，前端只渲染该事件。

**Tech Stack:** C++20、CMake、Catch2、FTXUI、SQLite。

## Global Constraints

- 所有新增或修改的用户可见文字、日志、Doxygen 注释均使用中文。
- 禁止在 Auto 模式自动批准包含 shell 组合、间接执行或包装器的命令。
- `Deny` 在所有前端及模式中都不可覆盖；无交互 Console 不得阻塞。
- 不实现完整 POSIX shell 解析器、策略持久化或配置文件。
- 每个行为变更先写失败测试，再写最小实现；每个任务结束运行其覆盖测试并独立提交。

---

## 文件职责

- `include/agent/command_policy.hpp` 与 `src/command_policy.cpp`：命令文本的保守分类。
- `include/agent/command_authorization.hpp` 与 `src/command_authorization.cpp`：策略到授权决定的共享映射。
- `include/agent/agent_event.hpp` 与 `include/agent/agent_loop.hpp`：拒绝事件与模型 Observation。
- `include/app_cli/command_review.hpp` 与 `src/command_review.cpp`：可注入流的 Console 审核器。
- `include/tui/tui_session.hpp`、`src/tui_session.cpp`、`src/tui_state.cpp`：TUI 审核适配与单次日志展示。
- `src/main.cpp`：TTY 探测、Console 审核器和拒绝事件输出。
- `CMakeLists.txt`、`tests/CMakeLists.txt`：编译及测试源注册。
- `tests/test_command_policy.cpp`、`tests/test_command_authorization.cpp`、`tests/test_command_review.cpp`、`tests/test_agent_loop.cpp`、`tests/test_tui_session.cpp`、`tests/test_tui_state.cpp`：回归测试。
- `docs/tui-integration-changes.md`：与实现一致的命令审批说明。

### Task 1: 加固命令分类

**Files:**
- Modify: `include/agent/command_policy.hpp`
- Modify: `src/command_policy.cpp`
- Modify: `tests/test_command_policy.cpp`

**Interfaces:**
- Produces: `PolicyResult evaluate_command_policy(std::string_view, const PolicyContext&)`，其中 `action` 为 `Allow`、`RequireReview` 或 `Deny`。

- [x] **Step 1: 写出失败的分类测试**

在 `tests/test_command_policy.cpp` 加入表驱动断言：`"echo ok; rm -rf cache"`、`"echo ok && rm cache"`、`"echo ok | rm cache"`、`"echo $(date)"`、`"sudo rm cache"`、`"sh -c 'rm cache'"`、`"echo ok > out"` 和含换行命令均为 `RequireReview`；`"/bin/rm cache"` 为 `Deny`，`"echo ok"` 为 `Allow`。

- [x] **Step 2: 运行测试确认失败**

Run: `cmake --build build -j2 && ./build/tests/swe_agent_tests "命令策略*"`

Expected: 新增的组合或包装器用例因当前首 token 解析而失败。

- [x] **Step 3: 实现保守扫描**

在 `src/command_policy.cpp` 先检查空白、换行及 `;`、`&&`、`||`、`|`、`<`、`>`、`` ` ``、`$(` 等组合标记，再检查首个程序是否为 `sudo`、`sh`、`bash` 或 `zsh`；上述输入返回带中文原因与稳定规则 ID 的 `RequireReview`。保留绝对路径 basename 的拒绝可执行文件检查，并为公开类型和函数补充中文 Doxygen 注释。

- [x] **Step 4: 运行测试确认通过**

Run: `cmake --build build -j2 && ./build/tests/swe_agent_tests "命令策略*"`

Expected: 通过，且直接拒绝仍优先于普通允许。

- [x] **Step 5: 提交任务**

Run: `git add include/agent/command_policy.hpp src/command_policy.cpp tests/test_command_policy.cpp && git commit -m "feat(policy): 保守识别复杂命令"`

### Task 2: 建立共享授权器与拒绝事件

**Files:**
- Create: `include/agent/command_authorization.hpp`
- Create: `src/command_authorization.cpp`
- Modify: `include/agent/agent_event.hpp`
- Modify: `include/agent/agent_loop.hpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/test_command_authorization.cpp`
- Modify: `tests/test_agent_loop.cpp`

**Interfaces:**
- Produces: `CommandDecision authorize_command(const CommandRequest&, const PolicyContext&, bool review_all, const CommandAuthorizer&)`。
- Produces: `AgentEventType::CommandRejected` 与 `AgentEvent::rule_id`；`CommandDecision` 也携带 `rule_id`。

- [x] **Step 1: 写出失败的授权与循环测试**

新增授权矩阵测试：Allow 在 `review_all=false` 时直接批准、Allow 在 `review_all=true` 时调用审核器、RequireReview 无审核器时拒绝、Deny 不调用审核器、审核器 Stop 原样返回。新增 Agent Loop 测试：审核器 Reject 后只收到一个 `CommandRejected`，事件包含命令、规则 ID、中文原因，shell 未运行，历史含拒绝 Observation。

- [x] **Step 2: 运行测试确认失败**

Run: `cmake --build build -j2 && ./build/tests/swe_agent_tests "共享授权*" "拒绝命令*"`

Expected: 因共享头文件、事件类型和循环事件尚不存在而失败。

- [x] **Step 3: 实现最小共享映射与事件流**

定义审核回调为现有 `CommandAuthorizer`；授权器先调用策略，再按矩阵返回：Deny 直接 Reject，Allow 且非 review-all 直接 Approve，其余调用审核器；没有审核器时以 `"需要人工审核，但当前没有可用的交互审核器。"` 拒绝。把策略 ID/原因复制到决定；人工拒绝使用稳定 ID `user_rejected`。Agent Loop 在 Reject 时先发 `CommandRejected`，再追加 `Host: 命令已拒绝。\n原因：...` Observation，并跳过 `run_shell`。为新增公开 API 添加中文 Doxygen 注释。

- [x] **Step 4: 注册源文件并运行测试确认通过**

在两个 CMake 文件注册新源和测试，然后运行：`cmake --build build -j2 && ./build/tests/swe_agent_tests "共享授权*" "拒绝命令*"`

Expected: 授权矩阵与拒绝数据流通过。

- [x] **Step 5: 提交任务**

Run: `git add CMakeLists.txt tests/CMakeLists.txt include/agent/command_authorization.hpp src/command_authorization.cpp include/agent/agent_event.hpp include/agent/agent_loop.hpp tests/test_command_authorization.cpp tests/test_agent_loop.cpp && git commit -m "feat(agent): 统一命令授权与拒绝事件"`

### Task 3: 接入 TUI 并保证单次展示

**Files:**
- Modify: `include/tui/tui_session.hpp`
- Modify: `src/tui_session.cpp`
- Modify: `src/tui_state.cpp`
- Modify: `tests/test_tui_session.cpp`
- Modify: `tests/test_tui_state.cpp`

**Interfaces:**
- Consumes: Task 2 的 `authorize_command`、`CommandRejected` 和 `CommandDecision::rule_id`。
- Produces: TUI Auto 仅审核 RequireReview，Review 审核所有非 Deny 命令；拒绝事件只写一条系统日志。

- [x] **Step 1: 写出失败的 TUI 测试**

新增测试：Auto 下 `"echo ok; rm cache"` 打开待审核命令；`"rm cache"` 不打开待审核命令且返回 Reject；向 `TuiState::apply_event` 输入 `CommandRejected` 后日志包含命令、规则 ID 和原因；随后 `resolve_command_approval(Reject)` 不增加第二条拒绝日志。

- [x] **Step 2: 运行测试确认失败**

Run: `cmake --build build -j2 && ./build/tests/swe_agent_tests "TUI*"`

Expected: 现有 TUI 自行映射策略，且状态层没有 `CommandRejected` 渲染，新增断言失败。

- [x] **Step 3: 使用共享授权器替换本地映射**

`TuiSession::authorize_command` 调用共享 `authorize_command`，以 `CommandMode::Review` 作为 review-all，并保留条件变量审核回调处理 Y、N 与 Stop。`TuiState::resolve_command_approval` 只清理待审核状态；`apply_event` 的 `CommandRejected` 分支添加一条中文系统日志。为变更后的公开方法添加中文 Doxygen 注释。

- [x] **Step 4: 运行测试确认通过**

Run: `cmake --build build -j2 && ./build/tests/swe_agent_tests "TUI*"`

Expected: TUI 策略、等待及单次日志测试全部通过。

- [x] **Step 5: 提交任务**

Run: `git add include/tui/tui_session.hpp src/tui_session.cpp src/tui_state.cpp tests/test_tui_session.cpp tests/test_tui_state.cpp && git commit -m "feat(tui): 复用共享命令授权"`

### Task 4: 实现 Console 审核器并接入主程序

**Files:**
- Create: `include/app_cli/command_review.hpp`
- Create: `src/command_review.cpp`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/test_command_review.cpp`

**Interfaces:**
- Produces: `CommandDecision review_console_command(const CommandRequest&, std::istream&, std::ostream&, bool interactive)`。
- Consumes: Task 2 的共享授权器；`main.cpp` 传入 `isatty(STDIN_FILENO) != 0`。

- [x] **Step 1: 写出失败的 Console 审核测试**

用 `std::istringstream` 与 `std::ostringstream` 覆盖：`Y`/`yes` 批准、`N`/`no` 拒绝、无效输入提示后重试、EOF 拒绝、`interactive=false` 不读取输入且拒绝；断言所有提示和原因均为中文。

- [x] **Step 2: 运行测试确认失败**

Run: `cmake --build build -j2 && ./build/tests/swe_agent_tests "Console 命令审核*"`

Expected: 审核器头文件和实现不存在而失败。

- [x] **Step 3: 实现流注入审核器与主程序接线**

审核器向传入输出流写中文提示，大小写无关接受 `y`/`yes`、`n`/`no`，无效回答循环；非交互或 EOF 返回 Reject。`main.cpp` 使用共享授权器和工作区 `PolicyContext`，仅让 RequireReview 经 Console 审核器；扩展 `print_agent_event` 的 `CommandRejected` 分支，在 stderr 输出命令、规则 ID 与原因。为公开 Console 函数添加中文 Doxygen 注释。

- [x] **Step 4: 注册源文件并运行测试确认通过**

Run: `cmake --build build -j2 && ./build/tests/swe_agent_tests "Console 命令审核*"`

Expected: 全部 Console 审核路径通过。

- [x] **Step 5: 提交任务**

Run: `git add CMakeLists.txt tests/CMakeLists.txt include/app_cli/command_review.hpp src/command_review.cpp src/main.cpp tests/test_command_review.cpp && git commit -m "feat(cli): 接入交互式命令审核"`

### Task 5: 更新文档并进行全量验证

**Files:**
- Modify: `docs/tui-integration-changes.md`

**Interfaces:**
- Consumes: 前四个任务的最终行为。
- Produces: 与运行时一致的中文审批说明。

- [x] **Step 1: 写出文档验收清单**

在文档中说明决策矩阵、TUI Auto/Review、Console TTY/非 TTY、保守分类边界、拒绝事件及其 Observation；删除“尚未接入审批”或等价的过时表述。

- [x] **Step 2: 更新中文文档**

将命令审批章节改为上述事实，明确复杂 shell 命令会请求审核而非声称实现完整 shell 解析。

- [x] **Step 3: 执行全量验证**

Run: `cmake --build build -j2 && ctest --test-dir build --output-on-failure && git diff --check`

Expected: 构建成功、CTest 全部通过、空白检查无输出。

- [x] **Step 4: 提交任务**

Run: `git add docs/tui-integration-changes.md && git commit -m "docs: 更新命令审批行为说明"`
