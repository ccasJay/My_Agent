# My_Agent 开发者文档实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立一套只描述当前实现、可供开发者学习和维护 My_Agent 的完整中文文档。

**Architecture:** 使用 README 作为精简入口，将安装配置、总体架构、Agent Loop、模块源码导读、Session/TUI 和故障排查拆成职责单一的专题文档。行为说明以当前源码和可执行命令为准，专题之间通过相对链接连接，避免重复维护同一事实。

**Tech Stack:** Markdown、Mermaid、C++20、CMake、CTest、CLI11、FTXUI、yaml-cpp、SQLite3、libcurl、nlohmann/json。

## Global Constraints

- 所有新增文档正文使用中文，源码标识符、命令和配置键保持原样。
- 只描述当前 `main` 分支已经实现且能够验证的行为。
- 不记录未来规划，不把 commandPolicy 通配符配置写成已有能力。
- README 保持精简，实现细节写入专题文档。
- 测试仅简要说明，不创建独立测试专题文档。
- 不修改运行行为、公共接口、配置格式或测试代码。
- 不写入 API key、个人环境值或其他敏感信息。
- 仓库内导航一律使用相对链接。

---

## 文件结构

| 文件 | 职责 |
| --- | --- |
| `README.md` | 项目入口、能力概览、快速开始和文档学习路径 |
| `docs/getting-started.md` | 环境准备、配置、构建、安装和首次运行 |
| `docs/configuration.md` | `.env`、`agent.yaml`、CLI 参数和覆盖关系 |
| `docs/architecture.md` | 分层、依赖方向、核心数据流和线程边界 |
| `docs/agent-loop.md` | Agent Loop 协议、状态转换、授权和停止语义 |
| `docs/modules.md` | 逐模块的核心类型、关键函数、依赖和源码索引 |
| `docs/session-and-tui.md` | Session 持久化与 TUI 交互、状态和 Worker 模型 |
| `docs/troubleshooting.md` | 当前实现支持范围内的错误现象、原因和检查方法 |
| `docs/session-persistence.md` | 保留现有专题内容，补充返回统一入口的导航 |
| `docs/tui-integration-changes.md` | 保留现有变更说明，补充指向当前开发者文档的导航 |

### Task 1: 项目入口、快速开始与配置参考

**Files:**
- Create: `README.md`
- Create: `docs/getting-started.md`
- Create: `docs/configuration.md`

**Interfaces:**
- Consumes: `CMakeLists.txt` 的依赖和目标；`src/main.cpp` 的配置查找与模式选择；`src/cli.cpp` 的 CLI 参数；`src/agent_loader.cpp` 和 `src/dotenv_loader.cpp` 的加载行为；`config/agent.yaml` 的字段。
- Produces: 后续专题文档统一引用的项目入口、构建命令和配置事实。

- [ ] **Step 1: 验证公开命令和构建入口**

Run:

```bash
cmake -S . -B build -DSWE_AGENT_BUILD_TESTS=ON
cmake --build build --parallel 2
./build/agent --help
ctest --test-dir build -N
```

Expected: CMake 配置与构建成功；帮助文本显示 `-t`、`-m`、`-c/--continue`；CTest 能发现 `swe_agent_tests`。

- [ ] **Step 2: 编写 README 入口**

`README.md` 依次写入以下内容：

```markdown
# My_Agent
项目定位和当前能力

## 核心能力
OpenAI 兼容模型、RUN 协议、本地 Shell、命令审批、Session、Console/TUI

## 快速开始
依赖提示、.env 示例、配置/构建/运行命令

## 使用方式
agent、agent -t、agent -c、agent -m 的最小示例

## 文档导航
按入门 → 架构 → Agent Loop → 模块 → Session/TUI → 排障排列链接

## 当前限制
协作式停止、非完整 Shell 解析、Shell 输出上限、OpenAI 兼容响应假设

## 测试
cmake --build 与 ctest 两条命令
```

快速开始中的 `.env` 示例只使用占位值：

```dotenv
OPENAI_BASE_URL=https://example.com/v1/chat/completions
OPENAI_API_KEY=your-api-key
OPENAI_MODEL=your-model
```

- [ ] **Step 3: 编写快速开始文档**

`docs/getting-started.md` 必须覆盖：

- C++20、CMake 3.20、libcurl、yaml-cpp、SQLite3、nlohmann/json；CLI11 和 FTXUI 由 CMake FetchContent 获取。
- `.env` 与 `config/agent.yaml` 必须能从当前目录或上一级目录找到，因此推荐从项目根目录或 `build/` 运行。
- Debug 构建、测试、直接运行、`scripts/rebuild_install.sh` 和 `cmake --install`。
- 默认无 `-t` 启动 TUI；出现 `-t` 时运行一次 Console 任务。
- 首次运行会按当前工作区创建 Session 数据库。

- [ ] **Step 4: 编写配置参考**

`docs/configuration.md` 明确记录：

- `.env` 必需键为 `OPENAI_BASE_URL`、`OPENAI_API_KEY`、`OPENAI_MODEL`。
- `agent.yaml` 支持 `agent.system`、必需的 `agent.user`、`agent.step_limit` 和可选 `model.model_name`。
- 当前 `main.cpp` 只使用 `agent_cfg` 的 Prompt 和 step limit；模型最终由 `.env` 的 `OPENAI_MODEL` 初始化，再由 CLI `-m` 覆盖。不得声称 `model.model_name` 当前会覆盖运行模型。
- `step_limit: 0` 表示不限制步数。
- `-c` 继续当前规范化工作区最近的 Session；`-t` 决定 Console/TUI；`-m` 覆盖本次模型。
- `SWE_AGENT_DATA_DIR` 只影响 Session 数据库存储目录。

- [ ] **Step 5: 检查链接和提交**

Run:

```bash
rg -n '通配符|allow_patterns|review_patterns|deny_patterns' README.md docs/getting-started.md docs/configuration.md
git diff --check
```

Expected: 不出现占位符或未实现功能表述；`git diff --check` 无输出。

Commit:

```bash
git add README.md docs/getting-started.md docs/configuration.md
git commit -m "docs: 添加开发者入口与配置指南"
```

### Task 2: 总体架构与 Agent Loop

**Files:**
- Create: `docs/architecture.md`
- Create: `docs/agent-loop.md`

**Interfaces:**
- Consumes: `src/main.cpp`、`include/agent/agent_loop.hpp`、`include/agent/agent_event.hpp`、`include/agent/agent_run_result.hpp`、`include/agent/command_authorization.hpp`、`src/command_authorization.cpp`、`src/command_policy.cpp`、`src/shell.cpp`、`src/model_client.cpp`。
- Produces: `docs/modules.md` 和 `docs/session-and-tui.md` 引用的总体边界、事件语义和主调用链。

- [ ] **Step 1: 编写总体架构文档**

`docs/architecture.md` 使用 Mermaid flowchart 展示：

```text
CLI/main
  -> Config + ModelClient + SqliteSessionStore
  -> SessionManager -> AgentSession -> Agent Loop
  -> ModelClient -> HttpClient
  -> CommandAuthorization -> CommandPolicy / Reviewer -> Shell
  -> AgentEvent / AgentRunResult -> Console 或 TuiSession/TuiState
```

正文说明 `swe_agent_core`、`swe_agent_tui_support` 和 `swe-agent` 三个 CMake 目标的边界；解释 Provider 抽象、Session 所有权、Console/TUI 复用核心循环、TUI 主线程与 Worker 线程的边界，以及依赖方向为何避免 FTXUI 进入核心库。

- [ ] **Step 2: 编写 Agent Loop 协议和时序**

`docs/agent-loop.md` 使用 Mermaid sequenceDiagram 展示：

```text
User -> AgentSession -> Provider
Provider -> Agent Loop: Assistant text
Agent Loop -> Parser: extract RUN
Agent Loop -> Authorizer: ordinary command only
Authorizer -> Shell: approved command
Shell -> History: Observation as User role
Agent Loop -> Provider: next step
Agent Loop -> Consumer: events and final result
```

必须准确说明：

- `RUN:` 必须位于单独一行，解析器取第一个非空的有效 `RUN:` 命令。
- 无有效 `RUN:` 时追加 Host hint 并继续。
- `echo COMPLETE_TASK` 必须与非空结论出现在同一回复；它绕过普通命令授权但仍实际通过 Shell 执行。
- 普通命令在执行前经过 `CommandAuthorizer`；拒绝结果作为 Observation 写回历史。
- step limit、空响应及停止检查点。
- 停止不会取消正在阻塞的 HTTP 请求或 Shell 子进程。
- `ProcessResult` 合并 stdout/stderr，输出最多保留 16 KiB，并继续排空管道。

- [ ] **Step 3: 记录当前命令策略边界**

在 `docs/agent-loop.md` 中增加命令授权小节，按当前优先顺序说明：

1. 空命令要求人工审核。
2. 直接执行 `reboot`、`shutdown`、`rm`、`rmdir`（含带路径程序名）直接拒绝。
3. 包含 `;|<>&`、反引号、换行或 `$(` 的复杂语法要求人工审核。
4. `sudo`、`sh`、`bash`、`zsh` 包装命令要求人工审核。
5. 其余命令默认允许；TUI Review 模式或 Console 的审核器仍可能要求用户确认。

明确注明策略只抽取第一个 token，不实现完整 POSIX Shell 解析，也不支持 YAML 通配符规则。

- [ ] **Step 4: 校验内容并提交**

Run:

```bash
rg -n 'COMPLETE_TASK|16 KiB|CommandAuthorizer|协作式' docs/architecture.md docs/agent-loop.md
git diff --check
```

Expected: 四类关键行为均可定位；Markdown 无空白错误。

Commit:

```bash
git add docs/architecture.md docs/agent-loop.md
git commit -m "docs: 说明总体架构与 Agent 循环"
```

### Task 3: 模块与源码导读

**Files:**
- Create: `docs/modules.md`

**Interfaces:**
- Consumes: `include/` 和 `src/` 下全部生产代码；`tests/` 仅用于建立简要测试索引；Task 2 的架构术语。
- Produces: 面向维护者的类型、函数、依赖和源码定位参考。

- [ ] **Step 1: 建立模块目录和统一模板**

`docs/modules.md` 按以下顺序建立章节：

```markdown
## 程序入口与 CLI
## 配置加载
## HTTP 与模型适配
## Agent Loop 与历史
## 命令解析、策略、审批与 Shell
## AgentSession 与 SessionManager
## SQLite Session Store 与数据路径
## TUI Session 与状态模型
## TUI 日志、视口、历史和状态动画
## TUI 绘制与事件循环
```

每章均使用“职责、核心类型、关键函数、调用关系、异常/副作用、源码索引、测试索引”七个小节；无内容的小节明确写“该模块不直接抛出异常”或实际边界，不留空标题。

- [ ] **Step 2: 编写核心与基础设施模块**

覆盖下列类型和函数，并使用相对链接指向声明与实现：

- `cli::Cli`、`Cli::RunOption::use_tui()`。
- `config::AgentConfig`、`load_agent()`、`load_env()`、`get_required()`。
- `http::HttpClient::post()`、`model::IProvider`、`Provider` concept、`ModelClient`、`OpenaiCompatible::query()`、`build_request_body()`。
- `agent::run()` 两个重载、`AgentEvent`、`AgentRunResult`、`HistoryHooks`、`append_history()`。
- `extract_run_command()`、`run_shell()`、`format_process_result()`、`ProcessResult`。
- `evaluate_command_policy()`、`authorize_command()`、Console reviewer。

- [ ] **Step 3: 编写 Session 与 TUI 模块**

覆盖下列类型和函数，并说明生命周期与线程边界：

- `AgentSession::create()`、`restore()`、`submit()`、`clear()`。
- `SessionManager::new_session()`、`continue_latest()`、`resume()`、`list_sessions()`、`submit()`。
- `ISessionStore`、`SqliteSessionStore`、`SessionSnapshot`、容量常量、`session_database_path()`。
- `TuiSession::start()`、`request_stop()`、命令审核等待、`snapshot()`、`stop_and_join()`。
- `TuiState::apply_event()`、`apply_result()`、`load_session()`。
- `TuiLogBlocks`、`LogViewport`、`PromptHistory`、`RunStatusAnimation`。
- `run()` in `src/tui.cpp`、`build_tui_view()` 及 `parse_session_command()`。

- [ ] **Step 4: 添加源码和测试索引并提交**

Run:

```bash
rg -n '^## |^### ' docs/modules.md
rg -n 'tests/test_' docs/modules.md
git diff --check
```

Expected: 十个模块章节均存在；每章至少包含一个对应测试文件或明确说明集成边界；无空白错误。

Commit:

```bash
git add docs/modules.md
git commit -m "docs: 添加模块与源码导读"
```

### Task 4: Session、TUI 与故障排查

**Files:**
- Create: `docs/session-and-tui.md`
- Create: `docs/troubleshooting.md`
- Modify: `docs/session-persistence.md`
- Modify: `docs/tui-integration-changes.md`

**Interfaces:**
- Consumes: `docs/architecture.md`、`docs/modules.md` 的术语；现有 Session/TUI 专题文档；`src/tui.cpp`、`src/tui_session.cpp`、`src/session_command.cpp`、`src/sqlite_session_store.cpp`、`src/session_paths.cpp` 的当前行为。
- Produces: 面向日常开发调试的交互、持久化和排障参考。

- [ ] **Step 1: 编写 Session 与 TUI 当前行为指南**

`docs/session-and-tui.md` 覆盖：

- 默认新建 Session，`-c` 恢复当前工作区最新 Session。
- `/new`、`/sessions`、`/resume <至少 8 位唯一前缀>`。
- 数据库路径、工作区隔离、WAL、权限、单消息 1 MiB 与单 Session 64 MiB 限制。
- System seed、连续 sequence、历史追加失败时内存回滚、恢复时的校验。
- TUI 状态、事件到日志的转换、Worker 生命周期、协作停止和命令审批等待。
- Auto/Review 命令模式的差异以及运行中不能切换。
- 常用按键只列当前实现，详细布局和滚动优化链接到 `tui-integration-changes.md`。

- [ ] **Step 2: 编写故障排查文档**

`docs/troubleshooting.md` 对每个问题使用“现象 → 原因 → 检查 → 处理”格式，覆盖：

- 找不到 `.env` 或 `agent.yaml`。
- `.env` 必需键缺失或为空。
- CMake 找不到系统依赖，或 FetchContent 无法获取 CLI11/FTXUI。
- 模型 API 非 2xx、JSON 结构不符合 OpenAI Compatible `choices[0].message.content`。
- 模型反复出现格式错误或到达 step limit。
- 命令被策略拒绝、需要审核但输入不可交互、用户拒绝。
- Shell 非零退出、信号终止和输出截断。
- Session 数据目录不可用、`HOME` 缺失、恢复前缀无匹配或歧义、容量超限。
- TUI 停止后仍等待当前 HTTP/Shell 返回。

- [ ] **Step 3: 给现有专题文档添加统一导航**

在 `docs/session-persistence.md` 和 `docs/tui-integration-changes.md` 开头各添加一段简短说明，链接到：

```markdown
返回开发者文档入口：../README.md · 当前 Session 与 TUI 指南：session-and-tui.md
```

不得重写现有专题历史，也不得更新其中的历史测试计数来冒充当前结果。

- [ ] **Step 4: 校验内容并提交**

Run:

```bash
rg -n '/new|/sessions|/resume|Auto|Review|64 MiB' docs/session-and-tui.md
rg -n '^### .*现象|现象' docs/troubleshooting.md
git diff --check
```

Expected: Session 命令、审核模式、容量限制与排障格式均可定位；无空白错误。

Commit:

```bash
git add docs/session-and-tui.md docs/troubleshooting.md docs/session-persistence.md docs/tui-integration-changes.md
git commit -m "docs: 补充 Session TUI 与故障排查"
```

### Task 5: 全量一致性与交付验证

**Files:**
- Modify: `README.md`
- Modify: `docs/getting-started.md`
- Modify: `docs/configuration.md`
- Modify: `docs/architecture.md`
- Modify: `docs/agent-loop.md`
- Modify: `docs/modules.md`
- Modify: `docs/session-and-tui.md`
- Modify: `docs/troubleshooting.md`

**Interfaces:**
- Consumes: Task 1–4 的全部文档和当前源码。
- Produces: 无断链、无占位、通过项目验证的最终文档集。

- [ ] **Step 1: 检查 Markdown 相对链接**

Run:

```bash
python3 -c 'import pathlib,re,sys; files=[pathlib.Path("README.md"),*pathlib.Path("docs").rglob("*.md")]; bad=[]; [(bad.append(f"{p}: {t}")) for p in files for t in re.findall(r"\[[^]]*\]\(([^)#]+)(?:#[^)]+)?\)",p.read_text()) if not re.match(r"^[a-z]+://",t) and not (p.parent/t).resolve().exists()]; print("\n".join(bad)); sys.exit(bool(bad))'
```

Expected: 无输出，退出码为 0。

- [ ] **Step 2: 检查未实现能力、占位符与敏感信息**

Run:

```bash
rg -n '真实密钥|sk-[A-Za-z0-9]' README.md docs/getting-started.md docs/configuration.md docs/architecture.md docs/agent-loop.md docs/modules.md docs/session-and-tui.md docs/troubleshooting.md
rg -n 'allow_patterns|review_patterns|deny_patterns|fnmatch' README.md docs/getting-started.md docs/configuration.md docs/architecture.md docs/agent-loop.md docs/modules.md docs/session-and-tui.md docs/troubleshooting.md
```

Expected: 两条命令均不命中。

- [ ] **Step 3: 验证构建、测试与格式**

Run:

```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
git diff --check
```

Expected: 构建成功；CTest 100% 通过；`git diff --check` 无输出。

- [ ] **Step 4: 审核文档与源码一致性**

逐项对照并修正：

- CLI 参数与 `src/cli.cpp`。
- 配置读取与 `src/main.cpp`、`src/agent_loader.cpp`、`src/dotenv_loader.cpp`。
- Agent Loop 与 `include/agent/agent_loop.hpp`。
- 命令策略与 `src/command_policy.cpp`、`src/command_authorization.cpp`。
- Session 与 `src/agent_session.cpp`、`src/session_manager.cpp`、`src/sqlite_session_store.cpp`。
- TUI 命令、状态和线程语义与 `src/tui.cpp`、`src/tui_session.cpp`、`src/tui_state.cpp`。

Expected: 文档不再包含任何无法由上述源码定位的当前行为声明。

- [ ] **Step 5: 提交最终校正**

若 Step 1–4 产生修改：

```bash
git add README.md docs
git commit -m "docs: 校验开发者文档一致性"
```

若没有修改，则不创建空提交。最后运行：

```bash
git status --short
git log --oneline --decorate -7
```

Expected: 工作区干净；本任务的文档提交全部位于 `feature/developer-documentation`。
