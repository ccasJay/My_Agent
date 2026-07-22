# My_Agent

My_Agent 是一个使用 C++20 编写的本地软件工程 Agent。它通过 OpenAI
兼容接口调用模型，按照约定解析模型回复中的 `RUN:` 命令，在本地 Shell
执行命令，并把执行结果作为下一轮上下文返回给模型。

项目同时提供全屏 TUI、一次性 Console 模式和 ACP v1 stdio 子进程，三者
共用 Agent Loop、命令授权和 Session 持久化实现。

## 核心能力

- 调用 OpenAI Compatible Chat Completions 接口。
- 通过 `RUN: <command>` 协议驱动本地 Shell。
- 在执行前对命令进行策略分类和人工审核。
- 持久化会话，并按当前工作区新建、继续或恢复会话。
- 默认提供 FTXUI 交互界面，也可执行单个 Console 任务。
- 通过 `agent-acp` 接入支持 ACP v1 的编辑器或其他 Client。
- 通过事件和运行结果解耦核心循环与界面展示。

## 快速开始

项目要求 CMake 3.20 及支持 C++20 的编译器，并依赖 libcurl、yaml-cpp、
SQLite3 和 nlohmann/json。完整环境说明见[快速开始](docs/getting-started.md)。

在项目根目录创建 `.env`：

```dotenv
OPENAI_BASE_URL=https://example.com/v1/chat/completions
OPENAI_API_KEY=your-api-key
OPENAI_MODEL=your-model
```

然后构建并启动：

```bash
cmake -S . -B build -DSWE_AGENT_BUILD_TESTS=ON
cmake --build build --parallel 2
./build/agent
```

仓库已提供 [`config/agent.yaml`](config/agent.yaml)。加载器要求其中的
`agent.user` 非空；当前 CLI/TUI 实际提交的任务来自输入框或 `-t` 参数。

## 使用方式

```bash
# 默认启动 TUI，并创建新 Session
./build/agent

# 执行一次 Console 任务后退出
./build/agent -t "检查当前项目结构"

# 继续当前工作区最近的 Session
./build/agent -c

# 为本次运行覆盖模型名
./build/agent -m "model-name"

# 由 ACP Client 启动的 stdio Agent 进程
./build/agent-acp --env-file .env --agent-config config/agent.yaml
```

命令行参数和配置优先级见[配置参考](docs/configuration.md)，协议接入方式见
[ACP 接入指南](docs/acp-integration.md)。

## 文档导航

建议按以下顺序阅读：

1. [快速开始](docs/getting-started.md)：准备环境并运行项目。
2. [总体架构](docs/architecture.md)：理解分层、依赖和线程边界。
3. [Agent Loop](docs/agent-loop.md)：理解模型与 Shell 的循环协议。
4. [模块与源码导读](docs/modules.md)：定位核心类型和关键函数。
5. [Session 与 TUI](docs/session-and-tui.md)：理解持久化和交互界面。
6. [ACP 接入指南](docs/acp-integration.md)：把 Agent 接入 ACP Client。
7. [故障排查](docs/troubleshooting.md)：定位常见运行问题。

## 当前限制

- Shell 命令解析和安全策略不是完整的 POSIX Shell 解析器。
- 停止通过 StopToken 传播；HTTP 会中止传输，Shell 会终止独立进程组。
- 单次 Shell 输出最多保留 16 KiB，超出部分会被排空但不保存。
- 模型响应必须兼容 `choices[0].message.content` 结构。
- 命令在本机执行；运行项目前应检查 Prompt 和命令审核模式。
- ACP 首版不支持 MCP、富媒体、Client FS/Terminal、认证和配置选项。

## 测试

```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```
