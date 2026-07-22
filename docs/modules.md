# 模块与源码导读

[返回开发者文档入口](../README.md) · [总体架构](architecture.md) ·
[Agent Loop](agent-loop.md)

本文按职责介绍生产代码。建议先阅读总体架构，再沿着
`main → SessionManager → AgentSession → agent::run` 主链路查阅本页。

ACP 路径从 `acp_main → AcpServer → AcpSessionRegistry/AcpPromptController`
进入同一个 `AgentSession → agent::run` 核心链路。

## 程序入口与 CLI

### 职责

解析进程参数，装配配置、Provider、Session Store 和界面，并选择 Console
或 TUI。入口只做组合，不实现 Agent Loop。

### 核心类型

- `cli::Cli`：封装 CLI11 `App` 和解析后的 `RunOption`。
- `Cli::RunOption`：保存 task、model、continue 标志，以及 `-t` 是否出现。

### 关键函数

- `Cli::parse(argc, argv)`：解析参数并记录 `task_option_->count()`。
- `RunOption::use_tui()`：只有未出现 `-t` 时返回 true。
- `main()`：查找配置、构造依赖、选择 Session，并启动对应前端。
- `print_agent_event()`：Console 的同步事件消费者。
- `acp_main()`：装配 ACP 配置、Provider、Store 和 JSONL 连接。

### 调用关系

`main()` 是 composition root；它构造 `ModelClient`、`SqliteSessionStore` 和
`SessionManager`，然后调用 `tui::run()` 或 `SessionManager::submit()`。

### 异常与副作用

配置、模型、数据库和 Session 异常最终由 `main()` 捕获并写入 stderr，退出
码为 1。CLI11 解析结果由 `Cli::exit()` 转换为退出码。

### 源码索引

- [`include/app_cli/cli.hpp`](../include/app_cli/cli.hpp)
- [`src/cli.cpp`](../src/cli.cpp)
- [`src/main.cpp`](../src/main.cpp)
- [`include/app_cli/acp_cli.hpp`](../include/app_cli/acp_cli.hpp)
- [`src/acp_cli.cpp`](../src/acp_cli.cpp)
- [`src/acp_main.cpp`](../src/acp_main.cpp)

### 测试索引

- [`tests/test_cli.cpp`](../tests/test_cli.cpp)

## 配置加载

### 职责

从 dotenv 文本读取字符串键值，从 YAML 读取 Prompt、step limit 和可选模型
字段，并对入口所需值做最小校验。

### 核心类型

- `config::AgentConfig`：`system_prompt`、`user_prompt`、可选
  `model_name` 和 `step_limit`。
- dotenv 结果：`std::unordered_map<std::string, std::string>`。

### 关键函数

- `load_env(path)`：逐行解析第一个 `=` 前后的键和值。
- `get_required(env, key)`：返回非空值，否则抛异常。
- `load_agent(path)`：用 yaml-cpp 读取已支持字段，并要求 `agent.user` 非空。

### 调用关系

`main()` 先加载两类配置，再把 `AgentConfig` 传给 `SessionManager`，把 dotenv
值转换为 `ModelConfig`。CLI `-m` 在构造 `ModelClient` 前覆盖模型名。

### 异常与副作用

文件无法打开、YAML 语法/类型错误、必需键缺失都会抛异常。加载器只读
文件，不修改进程环境。

### 源码索引

- [`include/config/dotenv_loader.hpp`](../include/config/dotenv_loader.hpp)
- [`src/dotenv_loader.cpp`](../src/dotenv_loader.cpp)
- [`include/config/agent_loader.hpp`](../include/config/agent_loader.hpp)
- [`src/agent_loader.cpp`](../src/agent_loader.cpp)

### 测试索引

- [`tests/test_config.cpp`](../tests/test_config.cpp)

## HTTP 与模型适配

### 职责

把内部消息转换为 OpenAI Compatible 请求，通过 libcurl 发送同步 POST，并
把响应文本转换为 `ModelResponse`。

### 核心类型

- `model::Message` / `MSG`：role 与 content 组成的消息及列表。
- `ModelConfig`：base URL、API key、模型名。
- `IProvider`：运行时查询接口；带 StopToken 的兼容重载默认委托旧接口。
- `Provider`：Agent Loop 使用的编译期 concept。
- `ModelClient`：上层门面，内部持有 `IProvider`。
- `OpenaiCompatible`：当前具体 Provider。
- `http::HttpResponse`：HTTP 状态码和响应正文。

### 关键函数

- `role_to_string()`：映射内部 role。
- `build_request_body()`：构造 `model` 与 `messages` JSON。
- `HttpClient::post()`：初始化 libcurl、配置 headers/body，并按 StopToken
  中止传输。
- `OpenaiCompatible::query()`：检查 2xx 并读取
  `choices[0].message.content`。
- `ModelClient::query()`：转发到当前 Provider。

### 调用关系

`agent::run()` 只依赖 Provider 契约；生产环境调用链为
`ModelClient → OpenaiCompatible → HttpClient → libcurl`。

### 异常与副作用

HTTP 请求同步访问网络。StopToken 触发时，libcurl 进度回调中止传输并转换为
`OperationCancelled`；curl 失败、非 2xx、JSON 解析失败或响应字段不符合预期
时抛异常。请求头包含 Bearer token，日志不应打印该值。

### 源码索引

- [`include/model/message.hpp`](../include/model/message.hpp)
- [`include/model/model.hpp`](../include/model/model.hpp)
- [`include/model/openai_format.hpp`](../include/model/openai_format.hpp)
- [`include/model/model_client.hpp`](../include/model/model_client.hpp)
- [`src/model_client.cpp`](../src/model_client.cpp)
- [`include/http/http_client.hpp`](../include/http/http_client.hpp)
- [`src/http_client.cpp`](../src/http_client.cpp)

### 测试索引

- [`tests/test_model.cpp`](../tests/test_model.cpp)；真实网络请求不在单元测试范围。

## Agent Loop 与历史

### 职责

管理模型—命令—Observation 循环、过程事件、终态和协作停止，并通过 hook
把历史追加交给可选的持久化层。

### 核心类型

- `AgentEvent` / `AgentEventType`：同步过程事件。
- `AgentRunOptions`：事件处理器、StopToken 和 CommandAuthorizer。
- `AgentRunResult` / `AgentRunStatus`：循环返回结果。
- `HistoryEntryKind`、`HistoryAppend`、`HistoryHooks`：历史语义与副作用接口。
- `StopSource` / `StopToken`：共享原子停止状态。

### 关键函数

- `agent::run(provider, config, history, options, hooks)`：可复用核心循环。
- `agent::run(provider, config, options)`：创建 System/User 初始历史的便捷重载。
- `append_history()`：先写内存、再调用 hook，失败时回滚内存。

### 调用关系

`AgentSession::submit()` 调用带 history 的重载；一次性调用可使用便捷重载。
循环调用 Provider、命令解析/授权/Shell，并向 Console 或 TUI 发事件。

### 异常与副作用

Provider、事件回调、授权器和 history hook 的异常不会在循环内统一吞掉，会
传播到调用方。正常的命令非零退出封装为 Observation，不抛异常。

### 源码索引

- [`include/agent/agent_loop.hpp`](../include/agent/agent_loop.hpp)
- [`include/agent/agent_event.hpp`](../include/agent/agent_event.hpp)
- [`include/agent/agent_run_result.hpp`](../include/agent/agent_run_result.hpp)
- [`include/agent/history.hpp`](../include/agent/history.hpp)
- [`include/agent/cancellation.hpp`](../include/agent/cancellation.hpp)

### 测试索引

- [`tests/test_agent_loop.cpp`](../tests/test_agent_loop.cpp)

## 命令解析、策略、审批与 Shell

### 职责

从模型文本提取命令，对普通命令分类和审核，通过系统 Shell 执行获批命令，
并生成结构化 Observation。

### 核心类型

- `ProcessResult` / `TerminationKind`：输出、退出码/信号和截断状态。
- `PolicyContext`、`PolicyResult`、`PolicyAction`：非交互策略输入和结果。
- `CommandRequest`、`CommandDecision`、`CommandAction`：前端授权接口。

### 关键函数

- `extract_run_command()`：返回第一个非空有效 `RUN:` 命令。
- `evaluate_command_policy()`：按程序名和复杂语法做保守分类。
- `authorize_command()`：组合 Policy、`review_all` 和 Reviewer。
- `review_console_command()`：终端审批。
- `run_shell()`：以子进程执行，可指定 cwd、StopToken，并限制保存输出。
- `format_process_result()`：生成给人和模型阅读的结果。
- `ProcessResult::success()`：仅退出码为 0 时返回 true。

### 调用关系

Agent Loop 先解析命令。完成命令直接执行；普通命令调用授权器，只有
`Approve` 才调用 Shell。TUI 和 Console 共享策略，但 Reviewer 实现不同。

### 异常与副作用

策略本身不执行命令。`run_shell()` 会启动独立进程组，合并 stdout/stderr；
stdin 连接 `/dev/null`，取消时先 SIGTERM、1 秒后 SIGKILL；
它把执行错误放进结果而非抛出。Console Reviewer 会读写标准流。

### 源码索引

- [`include/agent/shell.hpp`](../include/agent/shell.hpp)
- [`src/shell.cpp`](../src/shell.cpp)
- [`include/agent/process_result.hpp`](../include/agent/process_result.hpp)
- [`src/process_result.cpp`](../src/process_result.cpp)
- [`include/agent/command_policy.hpp`](../include/agent/command_policy.hpp)
- [`src/command_policy.cpp`](../src/command_policy.cpp)
- [`include/agent/command_authorization.hpp`](../include/agent/command_authorization.hpp)
- [`src/command_authorization.cpp`](../src/command_authorization.cpp)
- [`include/app_cli/command_review.hpp`](../include/app_cli/command_review.hpp)
- [`src/command_review.cpp`](../src/command_review.cpp)

### 测试索引

- [`tests/test_shell.cpp`](../tests/test_shell.cpp)
- [`tests/test_command_policy.cpp`](../tests/test_command_policy.cpp)
- [`tests/test_command_authorization.cpp`](../tests/test_command_authorization.cpp)
- [`tests/test_command_review.cpp`](../tests/test_command_review.cpp)

## AgentSession 与 SessionManager

### 职责

`AgentSession` 维护一个会话的 history 和可选持久化绑定；`SessionManager`
负责当前工作区内会话的新建、继续、按前缀恢复、列出和提交。

### 核心类型

- `AgentSession`：Provider 引用、配置、Store 指针、元数据和 history。
- `SessionManager`：Provider/Store 引用、配置、workspace、model 和 active
  Session 的 `unique_ptr`。

### 关键函数

- `AgentSession::create()`：让 Store 创建带 System seed 的新快照。
- `AgentSession::restore()`：校验快照并恢复历史。
- `AgentSession::submit()`：追加 User Prompt，配置 hook，调用 Agent Loop。
- `AgentSession::clear()`：重置内存和持久化消息，保留 Session ID。
- `SessionManager::new_session()` / `continue_latest()` / `resume()`：切换 active。
- `list_sessions()` / `active_snapshot()` / `submit()`：委托 Store 或 active。

### 调用关系

main 和 TUI 只操作 `SessionManager`，不各自维护第二套 Session。
`SessionManager` 创建/恢复 `AgentSession`，后者将历史副作用映射到 Store。

### 异常与副作用

没有 active Session 时，访问或提交抛 `std::logic_error`。恢复前缀少于 8
字符、无匹配或多匹配会抛 `SessionStorageError`。提交会修改内存和数据库。

### 源码索引

- [`include/agent/agent_session.hpp`](../include/agent/agent_session.hpp)
- [`src/agent_session.cpp`](../src/agent_session.cpp)
- [`include/agent/session_manager.hpp`](../include/agent/session_manager.hpp)
- [`src/session_manager.cpp`](../src/session_manager.cpp)

### 测试索引

- [`tests/test_agent_session.cpp`](../tests/test_agent_session.cpp)
- [`tests/test_session_manager.cpp`](../tests/test_session_manager.cpp)

## SQLite Session Store 与数据路径

### 职责

定义持久化抽象与数据结构，用 SQLite 实现事务性创建、追加、读取、重置、
更新模型和归档，并计算平台相关数据库路径。

### 核心类型

- `ISessionStore`：存储抽象接口。
- `SessionMetadata`、`SessionMessage`、`SessionSnapshot`、`SessionSummary`。
- `SessionSeed`、`SessionStoreLimits`、`SessionStorageError`。
- `SqliteSessionStore`：Pimpl 隐藏 sqlite3 细节，不可拷贝、可移动。

### 关键函数

- `create_session()`：生成 ID，写入 metadata 和 sequence 0 System 消息。
- `append_message()`：检查 sequence 和容量后事务写入。
- `load_session()` / `latest_session()` / `list_sessions()`：查询快照或摘要。
- `reset_session()` / `update_model()` / `archive_session()`：修改状态。
- `session_database_path()`：处理覆盖目录与平台默认路径。

### 调用关系

main 创建 Store，`SessionManager` 管理查询，`AgentSession` 通过
`HistoryHooks` 追加消息。TUI 只通过 Manager 间接访问持久化。

### 异常与副作用

构造 Store 会创建目录/数据库、初始化 schema、设置权限、WAL、foreign
keys 和 busy timeout。SQLite 错误、容量超限、sequence 不连续或记录损坏
均转成 `SessionStorageError`。单消息默认 1 MiB，单 Session 默认 64 MiB。

### 源码索引

- [`include/agent/session_store.hpp`](../include/agent/session_store.hpp)
- [`include/agent/sqlite_session_store.hpp`](../include/agent/sqlite_session_store.hpp)
- [`src/sqlite_session_store.cpp`](../src/sqlite_session_store.cpp)
- [`include/agent/session_paths.hpp`](../include/agent/session_paths.hpp)
- [`src/session_paths.cpp`](../src/session_paths.cpp)

### 测试索引

- [`tests/test_session_store.cpp`](../tests/test_session_store.cpp)

## ACP 协议适配

### 职责

通过 JSONL stdio 暴露 ACP v1，把协议 Session 映射到现有持久化
`AgentSession`，并把 AgentEvent、工具执行和命令权限转换为 ACP 消息。

### 核心类型

- `JsonRpcConnection`：线程安全的单行 JSON-RPC 读写与反向请求 ID。
- `AcpServer`：初始化、方法分发、参数校验和历史回放。
- `AcpSessionRegistry`：管理一条连接内的多个活动 Session。
- `AcpPromptController`：管理全局唯一 Prompt Worker、取消和权限响应。

### 关键函数

- `AcpServer::run()`：读取直到 EOF，区分请求、通知和反向请求响应。
- `AcpSessionRegistry::create/load/resume/close()`：映射持久化生命周期。
- `AcpPromptController::start()`：注入 cwd-aware Shell Executor 后提交任务。
- `handle_event()`：映射消息块、工具 pending/in_progress/终态。
- `request_permission()`：等待 Client 返回 allow_once/reject_once。

### 调用关系

协议线程持续读取 stdio；Prompt Worker 调用 `AgentSession::submit()`。两条线程
通过 `JsonRpcConnection` 的写锁共享 stdout，通过 Prompt 状态和条件变量配对
权限响应。Registry 和 Console/TUI 使用相同的 SQLite Store。

### 异常与副作用

无效 JSON/请求返回标准 JSON-RPC 错误。请求级异常记录到 stderr，stdout
保持协议纯净。close/EOF 会请求取消并 join；生产 HTTP/Shell 会响应
StopToken，中止传输或终止子进程组。首版能力边界详见
[ACP 接入指南](acp-integration.md)。

### 源码索引

- [`include/acp/json_rpc.hpp`](../include/acp/json_rpc.hpp)
- [`include/acp/acp_server.hpp`](../include/acp/acp_server.hpp)
- [`include/acp/session_registry.hpp`](../include/acp/session_registry.hpp)
- [`include/acp/prompt_controller.hpp`](../include/acp/prompt_controller.hpp)
- [`src/acp_json_rpc.cpp`](../src/acp_json_rpc.cpp)
- [`src/acp_server.cpp`](../src/acp_server.cpp)
- [`src/acp_session_registry.cpp`](../src/acp_session_registry.cpp)
- [`src/acp_prompt_controller.cpp`](../src/acp_prompt_controller.cpp)

### 测试索引

- [`tests/test_acp_json_rpc.cpp`](../tests/test_acp_json_rpc.cpp)
- [`tests/test_acp_server.cpp`](../tests/test_acp_server.cpp)

## TUI Session 与状态模型

### 职责

在 Worker 线程运行同步 Agent 任务，协调停止和命令审批，并把事件/结果转换
为可由主线程安全读取的状态快照。

### 核心类型

- `TuiSession`：Worker、互斥锁、审批条件变量、StopSource 和 TuiState。
- `TuiState`：不加锁的任务状态机和语义日志。
- `TuiSnapshot`：状态文本、审批信息和增量日志的一致性只读快照。
- `CommandMode`：`Auto` 或 `Review`。

### 关键函数

- `TuiSession::start()`：拒绝空任务/并发任务，启动 Worker。
- `request_stop()` / `stop_and_join()`：请求协作停止并回收线程。
- `approve_command()` / `reject_command()`：提交等待中的审批决定。
- `toggle_command_mode()`：只在空闲时切换。
- `snapshot(known_revision)`：返回新增日志或要求全量重同步。
- `TuiState::apply_event()` / `apply_result()`：分别更新过程和真正终态。

### 调用关系

`tui::run()` 注入调用 `SessionManager::submit()` 的 TaskRunner。Worker 上的
事件回调修改 `TuiState`；主线程只读 `TuiSnapshot` 并提交用户操作。

### 异常与副作用

Worker 捕获任务异常并调用 `fail_task()`，避免异常跨线程传播。所有跨线程
状态访问必须持有 mutex；审批等待由停止或用户决定唤醒。析构会 join。

### 源码索引

- [`include/tui/tui_session.hpp`](../include/tui/tui_session.hpp)
- [`src/tui_session.cpp`](../src/tui_session.cpp)
- [`include/tui/tui_state.hpp`](../include/tui/tui_state.hpp)
- [`src/tui_state.cpp`](../src/tui_state.cpp)

### 测试索引

- [`tests/test_tui_session.cpp`](../tests/test_tui_session.cpp)
- [`tests/test_tui_state.cpp`](../tests/test_tui_state.cpp)

## TUI 日志、视口、历史和状态动画

### 职责

把语义日志整理为可折叠块，管理虚拟日志窗口和平滑滚动，保存输入历史，
并计算 spinner、活动文本和耗时。

### 核心类型

- `TuiLogBlock` / `TuiLogBlocks`：命令与结果合并后的展示模型。
- `LogViewport` / `LogWindow`：与 FTXUI 解耦的逻辑行视口。
- `PromptHistory`：当前进程内的任务输入历史和草稿。
- `RunStatusAnimation`：任务与阶段计时状态。

### 关键函数

- `TuiLogBlocks::append()` / `toggle()`：增量合并并返回首个变化块。
- `LogViewport::sync()` / `scroll_*()` / `tick()` / `render_window()`。
- `PromptHistory::record()` / `previous()` / `next()`。
- `run_spinner_frame()`、`format_run_duration()`、`RunStatusAnimation::sync()`。

### 调用关系

TUI 事件循环消费 `TuiSnapshot::new_logs`，更新日志块和视口；绘制层只接收
已计算的数据。动画线程触发刷新，状态对象本身不访问终端。

### 异常与副作用

这些组件主要操作内存，不直接抛出业务异常，也不访问文件或网络。输入
历史不会跨进程持久化。

### 源码索引

- [`include/tui/log_block.hpp`](../include/tui/log_block.hpp) / [`src/log_block.cpp`](../src/log_block.cpp)
- [`include/tui/log_viewport.hpp`](../include/tui/log_viewport.hpp) / [`src/log_viewport.cpp`](../src/log_viewport.cpp)
- [`include/tui/prompt_history.hpp`](../include/tui/prompt_history.hpp) / [`src/prompt_history.cpp`](../src/prompt_history.cpp)
- [`include/tui/run_status.hpp`](../include/tui/run_status.hpp) / [`src/run_status.cpp`](../src/run_status.cpp)

### 测试索引

- [`tests/test_log_block.cpp`](../tests/test_log_block.cpp)
- [`tests/test_log_viewport.cpp`](../tests/test_log_viewport.cpp)
- [`tests/test_prompt_history.cpp`](../tests/test_prompt_history.cpp)
- [`tests/test_run_status.cpp`](../tests/test_run_status.cpp)

## TUI 绘制与事件循环

### 职责

创建 FTXUI Screen 和输入组件，处理快捷键、Session 斜杠命令、日志选择和
动画通知，并把纯状态渲染为终端 Element。

### 核心类型

- `ActivePane`：Prompt 或 Scrollback 焦点。
- `LogLine` / `RenderedLog`：按终端宽度展开后的可见行与块位置。
- `SlashCommand` / `SlashRegistry` / `SlashContext`：可扩展的斜杠元命令
  接口、注册表与 UI 依赖注入；内置 `/new`、`/sessions`、`/resume`、`/help`。
- `SlashSuggestResult` / `SlashSuggestItem`：Prompt 斜杠建议下拉的纯逻辑结果。

### 关键函数

- `tui::run(SessionManager&, model_name)`：TUI composition root 和事件循环。
- `SlashRegistry::parse` / `dispatch`：解析并执行斜杠命令；非 `/` 输入返回
  `NotACommand` 以便提交普通任务。
- `register_builtin_slash_commands()`：注册内置 handler。
- `evaluate_slash_suggest` / `apply_slash_completion`：按光标 token 前缀
  过滤命令并写回补全（不执行）。
- `render_slash_suggest_panel()`：绘制建议列表。
- `make_log_lines()`：UTF-8 安全地将日志块转为显示行。
- `render_log_panel()`、`render_run_panel()`、`render_approval_panel()`、
  `render_prompt_panel()`、`render_header()`、`render_status_bar()`、
  `render_shortcuts()`：纯展示函数。

### 调用关系

`main()` 调用 `tui::run()`。事件循环维护输入、焦点、块缓存和 viewport，
从 `TuiSession` 取快照，再调用 `tui_view.cpp` 的渲染函数组合界面。空闲
Prompt 输入时 `on_change` 刷新建议列表；列表打开时 `Tab`/`Enter` 只补全。
关闭后 Enter 先走 `SlashRegistry::dispatch`；未匹配斜杠时才
`TuiSession::start`。

### 异常与副作用

该模块占用终端全屏并启动动画线程。斜杠命令异常被转换为界面 notice；
退出路径请求停止、join Worker 和动画线程，使终端恢复。

### 源码索引

- [`include/tui/tui.hpp`](../include/tui/tui.hpp)
- [`src/tui.cpp`](../src/tui.cpp)
- [`include/tui/tui_view.hpp`](../include/tui/tui_view.hpp)
- [`src/tui_view.cpp`](../src/tui_view.cpp)
- [`include/tui/slash_types.hpp`](../include/tui/slash_types.hpp)
- [`include/tui/slash_command.hpp`](../include/tui/slash_command.hpp)
- [`include/tui/slash_context.hpp`](../include/tui/slash_context.hpp)
- [`include/tui/slash_registry.hpp`](../include/tui/slash_registry.hpp)
- [`include/tui/slash_suggest.hpp`](../include/tui/slash_suggest.hpp)
- [`src/slash_registry.cpp`](../src/slash_registry.cpp)
- [`src/slash_context.cpp`](../src/slash_context.cpp)
- [`src/slash_builtins.cpp`](../src/slash_builtins.cpp)
- [`src/slash_suggest.cpp`](../src/slash_suggest.cpp)

### 测试索引

- [`tests/test_slash_registry.cpp`](../tests/test_slash_registry.cpp)
- [`tests/test_slash_suggest.cpp`](../tests/test_slash_suggest.cpp)
- 绘制组合与真实终端交互没有独立自动化测试；其状态、日志和视口依赖由
  上述 TUI 单元测试覆盖。
