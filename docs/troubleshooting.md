# 故障排查

[返回开发者文档入口](../README.md) · [快速开始](getting-started.md) ·
[配置参考](configuration.md)

排查时优先保留完整错误文本，但不要公开 `.env`、Authorization header 或
API key。以下处理方式只覆盖当前实现能够确认的行为。

## 找不到 `.env` 或 `agent.yaml`

### 现象

启动时报 `Could not find .env` 或 `Could not find agent.yaml`。

### 原因

程序只检查当前目录和上一级目录，不按二进制安装位置或用户配置目录查找。

### 检查

```bash
pwd
ls -la .env config/agent.yaml ../.env ../config/agent.yaml
```

### 处理

从项目根目录或 `build/` 运行，或把两个配置文件放到程序支持的相对位置。

## `.env` 必需键缺失

### 现象

错误指出 `OPENAI_BASE_URL`、`OPENAI_API_KEY` 或 `OPENAI_MODEL` 缺失/为空。

### 原因

入口对三个键都调用 `get_required()`。dotenv 加载器不展开变量，也不解析
Shell 风格的 export 语法。

### 检查

只检查键名，不输出值：

```bash
sed -n 's/=.*//p' .env
```

### 处理

按[配置参考](configuration.md)补齐键和值。不要把真实 key 复制到 issue 或
终端共享记录中。

## CMake 配置或依赖获取失败

### 现象

`find_package` 找不到 CURL、yaml-cpp、SQLite3、nlohmann/json，或
FetchContent 获取 CLI11/FTXUI 失败。

### 原因

前四项是系统/包管理器依赖；后两项在首次配置时可能需要网络和 Git。

### 检查

```bash
cmake -S . -B build -DSWE_AGENT_BUILD_TESTS=ON
```

查看首个 `Could NOT find` 或 FetchContent 错误，不要只看后续连锁错误。

### 处理

通过当前平台的包管理器安装缺失开发包；确认 Git 和依赖仓库可访问后重新
运行配置。无需删除整个仓库；依赖缓存位于 `build/_deps`。

## 模型请求失败

### 现象

出现 `curl request failed`、`Model API returned HTTP ...`、JSON 解析异常或
访问 `choices`/`message`/`content` 时的类型/键错误。

### 原因

- endpoint 不可访问、TLS/DNS 失败或请求超过连接 10 秒/总计 60 秒；
- API key、模型名或 endpoint 不被服务端接受；
- 服务端非 2xx；
- 响应不兼容 `choices[0].message.content`。

### 检查

确认 `.env` 的键存在，核对服务端文档中的完整 Chat Completions endpoint
和模型名。错误响应正文可能包含服务端诊断信息，但分享前应脱敏。

### 处理

修正 endpoint、凭据或模型名，或改用返回 OpenAI Compatible 响应的服务。
当前代码没有重试、流式响应或其他响应 schema fallback。

## 模型反复格式错误或达到 step limit

### 现象

日志出现 FormatError，模型继续回复但不执行；最终显示 Step limit reached。

### 原因

回复没有第一个非空的单行 `RUN: <command>`，或完成时只有
`RUN: echo COMPLETE_TASK` 而没有同一回复中的非空结论。

### 检查

查看 Assistant 日志和随后 Host hint，并检查 `config/agent.yaml` 中的协议
文本及 `agent.step_limit`。

### 处理

让 System Prompt 明确遵守 [`RUN:` 与完成协议](agent-loop.md#run-协议)。
调试时可合理提高 step limit；`0` 会取消限制，也可能让格式错误持续消耗
模型请求。

## 命令被拒绝或无法审核

### 现象

出现 `CommandRejected`、策略 rule ID，或 Console 报人工审核不可用。

### 原因

- 命令直接命中拒绝程序；
- 复杂 Shell 语法或 Shell wrapper 要求人工审核；
- Console stdin 不是 TTY，或审批输入 EOF；
- 用户在 Console/TUI 明确拒绝；
- TUI Review 模式要求审核所有非拒绝命令。

### 检查

查看 `rule_id` 和原因，并对照[命令授权规则](agent-loop.md#命令授权)。在 TUI
状态栏确认当前是 Auto 还是 Review。

### 处理

不要通过改写路径或包装命令规避拒绝策略。需要审核时使用交互终端并确认
命令内容；策略直接拒绝的命令无法由用户审批覆盖。

## Shell 命令失败、被信号终止或输出截断

### 现象

Observation 末尾出现 `[exit=N]`、`[signal=N]`、`[status=unknown]`、
`...[truncated]` 或 `[shell] ...`。

### 原因

子进程非零退出、被信号终止、pipe/fork/read/wait 失败，或合并输出超过
16 KiB。

### 检查

阅读 `$ <command>` 后的合并 stdout/stderr 和状态标记。必要时在相同工作
目录手工运行一个只读诊断命令。

### 处理

根据命令自身错误修正输入。输出截断时把诊断命令缩小范围或让命令把结果
汇总后输出；当前 Agent 不提供实时流式 Shell 日志。

## Session 数据库初始化或写入失败

### 现象

启动或提交时报 `SessionStorageError`、SQLite 错误、权限错误或容量超限。

### 原因

数据目录不可创建/写入、未设置 `HOME` 且无覆盖目录、数据库损坏、消息
sequence 不一致，或超过单消息 1 MiB/单 Session 64 MiB。

### 检查

先确认路径来源：

```bash
printf 'SWE_AGENT_DATA_DIR=%s\n' "${SWE_AGENT_DATA_DIR:-}"
printf 'HOME is %s\n' "${HOME:+set}"
```

然后检查错误中指向的目录权限。不要直接编辑生产数据库。

### 处理

将 `SWE_AGENT_DATA_DIR` 指向当前用户可写的专用目录，或修复默认数据目录
权限。容量超限时缩短单次输入/输出并创建新 Session；当前 TUI 不提供删除
或压缩 Session 的命令。

## Session 恢复失败

### 现象

`-c` 报当前 workspace 无历史 Session，或 `/resume` 报前缀过短、无匹配、
多匹配、消息损坏。

### 原因

Session 按规范化 workspace 隔离；`/resume` 前缀至少 8 字符且必须唯一；
恢复还会校验 System seed 和连续 sequence。

### 检查

在同一工作目录启动 TUI，使用 `/sessions` 查看可用的 8 位 ID 前缀。

### 处理

切换到创建 Session 时的 workspace，或使用更长的唯一前缀。校验失败的
数据库不要手工修补；先备份文件，再通过新 Session 继续工作。

## TUI 请求停止后仍在等待

### 现象

按 `Esc`、`Ctrl+C` 或退出快捷键后状态停留在 Stopping 一段时间。

### 原因

停止通过 StopToken 传播，但底层仍需要调度到检查点。HTTP 由 libcurl 进度
回调中止传输；Shell 先向独立进程组发送 SIGTERM，1 秒后仍未退出则发送
SIGKILL。处于内核不可中断状态的进程，或自行脱离进程组的后代，可能继续
拖延回收。

### 检查

查看停止前活动是 Thinking 还是具体命令。避免在同一 Session 中重复提交；
TUI 会保持 busy 直到旧 Worker 真正返回。

### 处理

先短暂等待取消流程完成，并查看 stderr 是否记录终止失败。若进程长期停留，
检查命令是否创建了脱离进程组的后台服务，或是否处于系统级不可中断 I/O；
这类进程需要在 Agent 外部单独管理。

## ACP Client 无法解析输出

### 现象

Client 报 JSON 解析失败、请求无响应，或把普通日志当作协议对象。

### 原因

- Client 没有按换行分隔 JSON-RPC；
- 启动脚本把 stderr 重定向或合并到了 stdout；
- 直接在交互终端启动 `agent-acp`，却没有提供 JSONL 请求。

### 检查与处理

确认 Client 分别捕获 stdout/stderr，且每次向 stdin 写入一个完整 JSON 对象
和换行。stdout 的每个非空物理行都应能独立解析。协议示例见
[ACP 接入指南](acp-integration.md#jsonl-交互示例)。

## ACP Session 无法恢复

### 现象

load/resume 返回 Invalid params、Session not found 或 cwd mismatch。

### 原因与处理

ACP 要求传入存在的绝对 `cwd`，且恢复目录必须等于创建时规范化后的
workspace。load 要求 `mcpServers` 明确为空数组；resume 可以省略该字段，
提供时也必须为空数组。先用 `session/list` 按 cwd 查询，再原样使用返回的
`sessionId`。`session/close` 不会删除历史，关闭后仍可重新 load/resume。

## ACP 返回 Server busy

整个进程只允许一个 Prompt 运行，单连接默认最多加载 64 个活动 Session。
等待当前 Prompt 结束，或用 `session/close` 释放不再使用的活动 Session 后
重试；重复加载已经活动的 Session 不会重复占用名额。

## ACP 权限请求超时

Client 必须在 5 分钟内响应 `session/request_permission`。超时会按 fail-closed
处理：对应工具先更新为 failed，随后 Prompt 以 cancelled 结束。应检查 Client
是否正确配对反向请求 ID，并确保没有把 stderr 合并进 JSONL stdout。
