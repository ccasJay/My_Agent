# ACP 接入指南

[返回开发者文档入口](../README.md) · [总体架构](architecture.md) ·
[配置参考](configuration.md)

My_Agent 提供独立的 `agent-acp` 子进程，以 ACP v1 和支持该协议的 Client
通信。它复用现有 Agent Loop、CommandPolicy 和 SQLite Session，不替换也不
修改 Console/TUI。协议定义以
[Agent Client Protocol 官方仓库](https://github.com/agentclientprotocol/agent-client-protocol)
为准；本页说明的是当前实现范围。

## 构建与启动

正常构建会同时生成两个可执行文件：

```bash
cmake -S . -B build -DSWE_AGENT_BUILD_TESTS=ON
cmake --build build --parallel 2
./build/agent-acp --help
```

`agent-acp` 是由 Client 管理生命周期的 stdio 子进程，不是面向人的交互式
命令。Client 应把 JSON-RPC 写入进程 stdin，并从 stdout 逐行读取响应和
通知。启动参数如下：

| 参数 | 用途 |
| --- | --- |
| `--env-file PATH` | 显式指定 dotenv 文件 |
| `--agent-config PATH` | 显式指定 Agent YAML 文件 |
| `-m, --model NAME` | 覆盖 `OPENAI_MODEL` |
| `-h, --help` | 显示帮助并退出 |

未显式指定文件时，`.env` 和 `agent.yaml` 仍按当前目录、上一级目录的既有
规则发现。Session 数据库仍由 `SWE_AGENT_DATA_DIR` 或平台默认目录决定。

Client 配置的核心等价于：

```json
{
  "command": "/absolute/path/to/build/agent-acp",
  "args": [
    "--env-file", "/absolute/path/to/.env",
    "--agent-config", "/absolute/path/to/config/agent.yaml"
  ]
}
```

实际字段名取决于所用 ACP Client。建议使用绝对路径，避免 Client 的启动
目录影响配置发现。

## 传输与进程约束

- stdin/stdout 使用 UTF-8 JSONL：一个完整 JSON-RPC 2.0 对象占一行。
- 每次写入在互斥锁内完成，Prompt Worker 与协议线程不会交错污染一行。
- stdout 只承载 JSON-RPC；诊断和运行时错误只写 stderr。
- EOF 会请求取消正在运行的 Prompt，等待 Worker 退出后结束进程。
- HTTP 和 Shell API 仍是同步调用，但都会检查 StopToken：HTTP 中止传输，
  Shell 终止独立进程组。

不要把 stderr 合并回 stdout，否则 Client 会把日志误当作协议消息。

`initialize.params.protocolVersion` 必须是 0 到 65535 的整数。请求版本 1 时
原样返回；其他合法版本会协商到当前支持的版本 1。负数、超出范围或非整数
返回 Invalid params。

## 能力矩阵

| 能力 | 当前状态 | 说明 |
| --- | --- | --- |
| `initialize` | 支持 | 协商并返回稳定的 `protocolVersion: 1` |
| `session/new` | 支持 | 创建 SQLite Session 并加载到当前连接 |
| `session/list` | 支持 | 可按 `cwd` 过滤，使用不透明游标分页 |
| `session/load` | 支持 | 恢复 Session，并回放可见对话历史 |
| `session/resume` | 支持 | 恢复 Session，不回放历史 |
| `session/close` | 支持 | 取消关联 Prompt 并从内存 Registry 释放 |
| `session/prompt` | 支持 | 接受 `text` 与 `resource_link` 内容块 |
| `session/cancel` | 支持 | 作为通知请求协作式取消 |
| `session/update` | 支持 | 输出消息增量和工具状态通知 |
| `session/request_permission` | 支持 | Agent 向 Client 发起反向权限请求 |
| MCP Server | 不支持 | `mcpServers` 首版必须为空数组 |
| Client FS/Terminal | 不支持 | 不声明相应能力，也不发起相关请求 |
| 富媒体 | 不支持 | 不接受 image、audio、resource 等内容块 |
| 认证与配置选项 | 不支持 | `authMethods` 为空，不暴露配置能力 |
| Session 删除 | 不支持 | `close` 不归档、不清除、不删除 SQLite 数据 |

`additionalDirectories` 在 new/load/resume 中只能省略或传空数组；list 不接受
该字段。

## Session 生命周期

`session/new` 和 `session/load` 要求：

- `cwd` 是存在的绝对目录，服务端会规范化它；
- `mcpServers` 明确存在且为空数组；
- `additionalDirectories` 若出现，必须为空数组。

`session/resume` 的 `mcpServers` 可以省略；若提供，也必须为空数组。它对
`cwd` 和 `additionalDirectories` 的要求与 new/load 相同。

SQLite Session ID 直接作为 ACP `sessionId`，对 Client 应视作不透明字符串。
一条连接默认最多同时加载 64 个 Session，重复加载已活动 Session 不重复
计数；超过上限返回 Server busy。整个 `agent-acp` 进程最多运行一个 Prompt，
跨 Session 并发提交同样返回 Server busy。

`session/list` 每页最多返回 20 条记录，按更新时间和 Session ID 稳定倒序。
可传 `cwd` 只列出当前工作区，也可省略以跨工作区列出。`nextCursor` 是实现
细节生成的不透明值，Client 不应解析或修改。

`session/load` 通过 `session/update` 依次回放 User/Assistant 文本。Assistant
内容中的 `RUN:` 行不会展示给 Client；Observation 与 HostHint 不回放，但
仍保留在恢复后的模型上下文中。`session/resume` 只恢复上下文，不发送回放
通知。恢复时传入的 `cwd` 必须与持久化 workspace 完全一致。

`session/close` 只关闭当前连接中的活动对象。如果该 Session 正在运行 Prompt，
服务端会先请求取消并等待 Worker 返回；持久化历史仍可在之后 load/resume。
`session/load`、`session/resume` 和 `session/close` 成功时都返回 JSON 对象。

## Prompt、消息与工具映射

`session/prompt.params.prompt` 必须是非空内容块数组。当前转换规则为：

| ACP 内容块 | 内部表示 |
| --- | --- |
| `{"type":"text","text":"..."}` | 原样追加文本 |
| `{"type":"resource_link","name":"n","uri":"u"}` | 转成 Markdown 链接 `[n](u)` |

多个块之间插入一个空行。结果为空白或出现其他块类型时返回 Invalid params。

Agent 的可见回复映射为 `agent_message_chunk`。普通 Shell 命令映射为一个工具
调用，状态按以下顺序更新：

```text
tool_call(pending)
  -> tool_call_update(in_progress)
  -> tool_call_update(completed | failed)
```

`echo COMPLETE_TASK` 是 Agent Loop 的内部完成标记，不作为工具调用展示。
命令的合并输出，以及文本形式的退出码、信号或截断标记，会在最终工具更新
中返回。

Prompt 最终状态映射如下：

| AgentRunStatus | ACP `stopReason` |
| --- | --- |
| `Completed` | `end_turn` |
| `Stopped` | `cancelled` |
| `StepLimitReached` | `max_turn_requests` |
| `EmptyResponse` | `refusal` |

## 命令权限

ACP 使用与 Console/TUI 相同的 CommandPolicy，并把 Session `cwd` 同时作为
工作目录和 workspace root：

- `Allow`：直接执行，不向 Client 请求权限；
- `RequireReview`：发送 `session/request_permission` 反向请求；
- `Deny`：直接拒绝，Client 不能覆盖策略结论。

权限请求只提供 `allow_once` 与 `reject_once`。Client 返回未知选项、错误响应
或格式错误时，命令按拒绝处理；返回 cancelled 或在等待期间收到
`session/cancel` 时，Prompt 进入取消流程。权限请求最多等待 5 分钟；超时
采用 fail-closed，工具转为 failed，Prompt 返回 cancelled。已取消请求的迟到
响应会被忽略并写入 stderr 诊断，不会影响后续 Prompt。

取消、停止、权限超时或异常退出时，服务端会先把所有未终结工具更新为
failed，再返回 Prompt 的 cancelled 或错误响应，避免 Client 工具界面停留在
pending/in_progress。

Shell 子进程在 Session `cwd` 内执行。目录切换发生在 fork 后的子进程，
不会修改 `agent-acp` 进程的全局当前目录，也不会影响其他已加载 Session。
子进程 stdin 连接 `/dev/null`，取消时先向独立进程组发送 SIGTERM；1 秒后
仍未退出则升级为 SIGKILL。HTTP 请求通过 libcurl 进度回调响应取消。

## JSONL 交互示例

下面省略了部分 capability 字段，真实 Client 应按 JSON-RPC ID 配对，而不是
依赖消息的固定顺序：

```jsonl
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":1}}
{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":1,"agentCapabilities":{"loadSession":true},"agentInfo":{"name":"my-agent","title":"My Agent","version":"0.1.0"},"authMethods":[]}}
{"jsonrpc":"2.0","id":2,"method":"session/new","params":{"cwd":"/absolute/workspace","mcpServers":[]}}
{"jsonrpc":"2.0","id":2,"result":{"sessionId":"SESSION_ID"}}
{"jsonrpc":"2.0","id":3,"method":"session/prompt","params":{"sessionId":"SESSION_ID","prompt":[{"type":"text","text":"Summarize this project"}]}}
{"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"SESSION_ID","update":{"sessionUpdate":"agent_message_chunk","content":{"type":"text","text":"Done"}}}}
{"jsonrpc":"2.0","id":3,"result":{"stopReason":"end_turn"}}
```

需要审批时，服务端会主动发出带字符串 ID 的请求：

```jsonl
{"jsonrpc":"2.0","id":"agent-1","method":"session/request_permission","params":{"sessionId":"SESSION_ID","toolCall":{"toolCallId":"tool-1","title":"$ command","kind":"execute","status":"pending","rawInput":{"command":"command"}},"options":[{"optionId":"allow-once","name":"Allow once","kind":"allow_once"},{"optionId":"reject-once","name":"Reject","kind":"reject_once"}]}}
{"jsonrpc":"2.0","id":"agent-1","result":{"outcome":{"outcome":"selected","optionId":"allow-once"}}}
```

## 测试与排查

```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

协议测试使用内存双向通道、Fake Provider 和临时 SQLite，覆盖初始化、完整
工具状态序列、权限允许/拒绝/超时、跨 Session 并发拒绝、取消、全部终态映射、
Session 容量、存储异常、重启后的 list/load 和输入校验。Shell 与 HTTP 测试
还会验证阻塞调用可被 StopToken 中断。若 Client 报告 JSON 解析错误，优先
确认它只读取 stdout，且没有把 stderr 合并进去。
更多启动与存储问题见[故障排查](troubleshooting.md)。
