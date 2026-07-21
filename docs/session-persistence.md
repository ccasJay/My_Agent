# Session 持久化

## 行为

- `agent` 或 `agent -t "task"` 默认创建新 Session。
- `agent -c` 或 `agent -c -t "task"` 恢复当前工作区最近更新的 Session。
- `-m` 会覆盖本次使用的模型，并同步到恢复后的 Session 元数据。
- TUI 中可使用：
  - `/new`：创建并切换到新 Session。
  - `/sessions`：列出当前工作区最近的 Session。
  - `/resume <id-prefix>`：按至少 8 位且唯一的 ID 前缀恢复 Session。

Session 按规范化后的当前工作区隔离，`-c` 不会恢复其他工作区的记录。

## 分层

```text
main / TUI / Console
        |
        v
 SessionManager      负责创建、恢复、切换和提交
        |
        v
  AgentSession       持有当前 history 和 Session ID
        |
        +----> Agent Loop
        |        每次 history append 通过 HistoryHooks 做检查点
        v
 ISessionStore
        |
        v
SqliteSessionStore   sessions + messages
```

TUI 不维护第二套会话对象。普通任务和 Session 命令都复用
`SessionManager`，Console 与 TUI 因而具有相同的持久化语义。

## 存储位置与边界

数据库文件为 `agent.db`：

- `SWE_AGENT_DATA_DIR` 已设置：`$SWE_AGENT_DATA_DIR/agent.db`
- macOS：`~/Library/Application Support/swe-agent/agent.db`
- Linux：`$XDG_DATA_HOME/swe-agent/agent.db`，未设置时使用
  `~/.local/share/swe-agent/agent.db`

目录权限设为仅当前用户可访问，数据库文件设为仅当前用户可读写。
API key、endpoint 和环境变量不会写入 Session。单条消息默认最多 1 MiB，
单个 Session 默认最多 64 MiB；超限写入在事务中回滚。

## 数据一致性

- 新 Session 首条记录固定为 System 消息。
- User prompt、Assistant、Observation 和 Host hint 按连续 sequence 保存。
- 持久化失败时，对应的内存 append 会回滚并向上抛出错误。
- 恢复时校验 System seed 和 sequence 连续性，并恢复保存的 system prompt；
  step limit 和模型名使用当前命令行或配置。
- SQLite 开启 foreign keys、WAL、busy timeout，并使用事务保护创建、追加和清空。
