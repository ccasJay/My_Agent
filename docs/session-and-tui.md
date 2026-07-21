# Session 与 TUI

[返回开发者文档入口](../README.md) · [总体架构](architecture.md) ·
[Session 持久化专题](session-persistence.md) ·
[TUI 集成变更说明](tui-integration-changes.md)

## Session 生命周期

进程启动后必须先选出一个 active Session：

- 默认调用 `SessionManager::new_session()`；
- 使用 `-c` 时调用 `continue_latest()`，恢复当前 workspace 最近更新的记录；
- TUI 运行期间可通过斜杠命令切换 active Session。

workspace 是启动时当前目录的规范化绝对路径。列表、最新会话和恢复均按
workspace 隔离，不会自动使用其他项目目录的记录。

### TUI Session 命令

| 命令 | 行为 |
| --- | --- |
| `/new` | 创建并切换到新 Session |
| `/sessions` | 列出当前 workspace 最近的 Session，显示 8 位 ID 前缀、标题和模型 |
| `/resume <id-prefix>` | 恢复唯一匹配的 Session；前缀至少 8 个字符 |

命令两侧空白会被去除。`/resume` 缺少参数、参数含空白或未知 `/` 命令会
显示错误 notice。任务运行期间输入区不接受新的 Session 命令。

## 持久化数据

SQLite 数据库文件名固定为 `agent.db`。路径优先级为：

1. `$SWE_AGENT_DATA_DIR/agent.db`；
2. macOS：`$HOME/Library/Application Support/swe-agent/agent.db`；
3. Linux：`$XDG_DATA_HOME/swe-agent/agent.db`；
4. Linux fallback：`$HOME/.local/share/swe-agent/agent.db`。

Store 初始化目录和 schema，启用 foreign keys、WAL 和 busy timeout。数据
目录限制为当前用户访问，数据库文件限制为当前用户读写。

### 数据模型与一致性

每个 Session 包含 metadata 和连续编号的 messages：

- sequence 0 固定为 System seed；
- 后续消息记录 role、语义 kind、正文和时间；
- `HistoryHooks` 在内存追加后同步写库；写库失败时回滚该次内存追加；
- 恢复时校验消息非空、System seed 和 sequence 连续性；
- 保存的 System Prompt 随 Session 恢复；step limit 和模型使用本次配置；
- `clear()` 保留 Session ID，但重置消息为新的 System seed。

单条消息默认最多 1 MiB，单个 Session 累计正文默认最多 64 MiB。容量、
sequence 和数据库更新在事务内检查，失败以 `SessionStorageError` 报告。

## TUI 状态模型

`TuiState` 是不加锁的纯状态机，`TuiSession` 用 mutex 保护它。主要状态为：

| 状态 | 含义 |
| --- | --- |
| `Ready` | 可提交任务 |
| `Running` | Worker 正在请求模型、审核或执行命令 |
| `Stopping` | 已请求停止，等待 Worker 到达检查点 |
| `Stopped` | Worker 已以停止结果返回 |
| `StepLimitReached` | 达到当前 step limit |
| `EmptyResponse` | Provider 返回空内容 |
| `Error` | Worker 捕获异常或无法启动 |

`Running` 内部再区分 Thinking、AwaitingApproval 和 RunningCommand。事件只
更新过程展示；只有 Worker 返回的 `AgentRunResult` 才会清除 busy 状态。

## Worker 与刷新

FTXUI 主线程处理输入和绘制；`TuiSession` Worker 同步执行
`SessionManager::submit()`。事件回调运行在 Worker 上，在锁内更新状态，
然后用线程安全的 `PostEvent()` 请求刷新。

`snapshot(known_log_revision)` 返回当前状态和 revision 之后的增量日志。
如果调用方的 revision 已不属于当前历史，它会标记 `full_resync` 并返回
完整日志。刷新通知和动画通知使用原子标志合并，避免事件队列持续堆积。

停止请求设置 `StopToken` 并唤醒可能等待命令审批的 Worker。HTTP 或 Shell
正在阻塞时不会被强制中断，必须等调用返回后才能完成停止和 join。

## 命令审核模式

TUI 初始模式是 `Review`，空闲时按 `Shift+Tab` 在两种模式间切换：

- `Auto`：策略 `Allow` 的命令直接执行；`RequireReview` 仍需确认；
- `Review`：策略未拒绝的所有普通命令都需确认。

运行中不能切换，避免同一任务使用两套授权语义。策略 `Deny` 在两种模式
下都直接拒绝，用户不能覆盖。等待审批时按 `Y` 批准，按 `N` 拒绝；停止
请求会取消等待并让 Agent Loop 返回 `Stopped`。

## 日志模型

`TuiState` 先产生 Task、Assistant、Command、Observation、Final、System
和 Error 语义日志。`TuiLogBlocks` 再把命令开始与对应结果合并为一个可折叠
块：运行时展开，完成后自动折叠。Scrollback 获得焦点后可选择并切换块。

`LogViewport` 只管理逻辑行、目标行和尾部跟随，不依赖 FTXUI。绘制层按
终端宽度生成 UTF-8 安全的显示行，并只构建可见窗口。更详细的性能设计见
[TUI 集成变更说明](tui-integration-changes.md)。

## 常用按键

| 场景 | 按键 | 行为 |
| --- | --- | --- |
| 空闲 Prompt | `Enter` | 提交任务或 Session 命令 |
| 空闲 Prompt | `↑` / `↓` | 浏览输入历史并保留原草稿 |
| 空闲 | `Tab` | 在 Prompt 与 Scrollback 间切换 |
| 空闲 | `Shift+Tab` | 切换 Auto/Review 审核模式 |
| Scrollback | `Ctrl+↑` / `Ctrl+↓` | 选择上一/下一日志块 |
| Scrollback | `Enter` | 展开或折叠选中命令块 |
| 审批中 | `Y` / `N` | 批准或拒绝待执行命令 |
| 运行中 | `Esc` / `Ctrl+C` | 请求停止 |
| 空闲 | `Ctrl+C` | 清空输入；输入已空时再次按下退出 |
| 任意 | `PgUp` / `PgDn` | 滚动 5 行 |
| 任意 | 鼠标滚轮 | 滚动 3 行 |
| 任意 | `Home` / `End` | 跳到日志顶部/底部 |
| 任意 | `Ctrl+Q` | 空闲时退出；运行时停止后退出 |
| 运行中 | `Ctrl+D` | 停止并在 Worker 返回后退出 |

完整键位与布局历史见 [TUI 集成变更说明](tui-integration-changes.md)。

## 继续阅读

- [Agent Loop](agent-loop.md)：事件、停止和命令授权语义。
- [模块与源码导读](modules.md)：`SessionManager`、Store、`TuiSession` 和
  `TuiState` 的接口定位。
- [故障排查](troubleshooting.md)：恢复失败、数据库和停止等待问题。
