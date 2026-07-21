# 配置参考

[返回开发者文档入口](../README.md) · [快速开始](getting-started.md)

## 配置来源

运行时配置来自三处：

1. `.env`：endpoint、API key 和默认模型名；
2. `config/agent.yaml`：Prompt 与 Agent step limit；
3. CLI 参数：本次运行的模式、模型覆盖和 Session 恢复方式。

程序只从当前工作目录和它的上一级目录寻找 `.env` 与
`config/agent.yaml`。文件不存在时，程序在初始化模型或界面前直接报错。

## `.env`

以下键均为必需项，缺失或值为空都会抛出运行时错误：

| 键 | 用途 |
| --- | --- |
| `OPENAI_BASE_URL` | OpenAI Compatible Chat Completions 完整 endpoint |
| `OPENAI_API_KEY` | 作为 Bearer token 发送的 API key |
| `OPENAI_MODEL` | 未被 CLI 覆盖时使用的模型名 |

示例：

```dotenv
OPENAI_BASE_URL=https://example.com/v1/chat/completions
OPENAI_API_KEY=your-api-key
OPENAI_MODEL=your-model
```

dotenv 加载器忽略空行和以 `#` 开头的行，以第一个 `=` 分隔键和值，并会
去掉键和值两侧的空白。值被一对匹配的单引号或双引号包围时，加载器会
去掉外层引号；它不实现变量展开、转义序列解码或多行值。

## `config/agent.yaml`

当前加载器识别以下字段：

| 字段 | 必需 | 当前用途 |
| --- | --- | --- |
| `agent.system` | 否 | 新 Session 的 System Prompt；缺失时为空字符串 |
| `agent.user` | 是 | 加载器要求非空；便捷版 `agent::run()` 可用作初始 User Prompt |
| `agent.step_limit` | 否 | 最大循环步数；默认值为 1，`0` 表示不限制 |
| `model.model_name` | 否 | 会被解析到 `AgentConfig`，但当前入口没有用它覆盖运行模型 |

最小示例：

```yaml
agent:
  system: |
    You are a careful software-engineering assistant.
  user: |
    Inspect the current project.
  step_limit: 10
```

仓库默认配置还约定模型以单独一行 `RUN: <command>` 请求本地命令，并以
带有非空结论的 `RUN: echo COMPLETE_TASK` 完成任务。这个约定由
[Agent Loop](agent-loop.md)实现。

当前 `main()` 通过持久化 `AgentSession` 运行任务，实际 User Prompt 来自
TUI 输入或 CLI `-t`，不会自动提交 `agent.user`。该字段仍因加载器校验而
必须非空，并会被不带外部 history 的 `agent::run(provider, config)` 便捷
重载使用。

### `model.model_name` 的当前状态

`load_agent()` 会读取 `model.model_name`，但 `main()` 当前使用 `.env` 中的
`OPENAI_MODEL` 创建 `ModelConfig`，只接受 CLI `-m` 覆盖。因此不要依赖
YAML 的该字段改变实际请求模型。这是当前实现边界，不是配置优先级中的
有效一层。

## CLI 参数

| 参数 | 行为 |
| --- | --- |
| `-t TEXT` | 进入 Console 模式并提交一次任务；参数是否出现决定模式 |
| `-m TEXT` | 覆盖本次运行的模型名 |
| `-c`, `--continue` | 恢复当前工作区最近更新的 Session，而不是创建新 Session |
| `-h`, `--help` | 显示帮助并退出，不继续加载配置 |

没有 `-t` 时进入 TUI。显式传入 `-t ""` 仍然进入 Console，只是提交空
任务字符串。

实际生效顺序如下：

```text
运行模型：OPENAI_MODEL -> CLI -m（若非空）
System Prompt/步数：config/agent.yaml
任务文本：TUI 输入或 CLI -t
启动模式：是否出现 -t
Session：默认新建；出现 -c 时恢复当前工作区最新记录
```

恢复 Session 时，保存的 System Prompt 随历史恢复；step limit 使用当前
配置，模型名使用本次 `.env` 或 `-m` 的结果。

## Session 数据目录

`SWE_AGENT_DATA_DIR` 只改变 SQLite 数据库目录，不影响 `.env`、YAML 或
工作区路径。数据库文件名固定为 `agent.db`：

| 条件 | 路径 |
| --- | --- |
| 设置 `SWE_AGENT_DATA_DIR` | `$SWE_AGENT_DATA_DIR/agent.db` |
| macOS 默认 | `$HOME/Library/Application Support/swe-agent/agent.db` |
| Linux 且设置 `XDG_DATA_HOME` | `$XDG_DATA_HOME/swe-agent/agent.db` |
| Linux 其他情况 | `$HOME/.local/share/swe-agent/agent.db` |

未设置覆盖目录且无法读取 `HOME` 时，Session 初始化抛出
`SessionStorageError`。

## 安全注意事项

- `.env` 含 API key，不应提交到版本库或粘贴到日志。
- `agent.yaml` 可以提交，但其中的 Prompt 会影响模型请求和本地命令行为。
- endpoint 的错误响应正文可能进入异常信息；Provider 不会主动过滤服务端
  返回内容。
- 命令策略和审核模式见[Agent Loop](agent-loop.md)，Prompt 中的安全要求
  不能替代主机侧策略。
