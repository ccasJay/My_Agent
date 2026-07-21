# 快速开始

[返回开发者文档入口](../README.md)

## 环境要求

项目使用 C++20 和 CMake 3.20。配置阶段需要能够找到以下系统依赖：

- 支持 C++20 的 Clang 或 GCC；
- libcurl；
- yaml-cpp；
- SQLite3；
- nlohmann/json；
- POSIX Shell 与 `popen()`、`pclose()` 等接口。

CLI11 v2.4.2 和 FTXUI v7.0.1 由 CMake `FetchContent` 获取。首次配置时若
本地尚无依赖源码，需要可访问对应的 Git 仓库。

## 准备运行配置

程序启动时依次寻找：

- 当前目录的 `.env`，找不到时尝试 `../.env`；
- 当前目录的 `config/agent.yaml`，找不到时尝试
  `../config/agent.yaml`。

因此推荐从项目根目录或它的 `build/` 目录运行程序。其他目录即使执行的是
已安装二进制，也不会自动查找仓库中的配置文件。

在项目根目录创建 `.env`：

```dotenv
OPENAI_BASE_URL=https://example.com/v1/chat/completions
OPENAI_API_KEY=your-api-key
OPENAI_MODEL=your-model
```

不要提交 `.env`。`OPENAI_BASE_URL` 应指向接受 OpenAI Compatible Chat
Completions 请求的完整 endpoint。

仓库中的 [`config/agent.yaml`](../config/agent.yaml) 定义 System Prompt、
加载器要求的 User Prompt 和 step limit。当前 CLI/TUI 的实际任务来自输入
或 `-t`；`agent.user` 仍必须非空。完整字段见[配置参考](configuration.md)。

## 配置与构建

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSWE_AGENT_BUILD_TESTS=ON
cmake --build build --parallel 2
```

构建产物是 `build/agent`。主要 CMake 目标包括核心库、TUI 支持库、最终
可执行文件和一个 Catch2 测试可执行文件。

## 运行测试

```bash
ctest --test-dir build --output-on-failure
```

CTest 当前注册一个聚合测试目标 `swe_agent_tests`，其中包含各模块的单元
测试。

## 首次运行

### TUI 模式

不提供 `-t` 时进入全屏 TUI：

```bash
./build/agent
```

程序会创建新 Session，然后等待输入任务。输入任务并按 `Enter` 提交。
Session 与 TUI 操作见[Session 与 TUI](session-and-tui.md)。

### Console 模式

只要命令行中出现 `-t` 就进入 Console 模式，包括 `-t ""`：

```bash
./build/agent -t "查看当前目录并总结项目结构"
```

Console 模式执行一次任务后退出。标准输入是交互式终端时，需要人工审核的
命令会显示审批提示；非交互式输入无法完成审核，命令会被安全拒绝。

首次启动会为规范化后的当前工作区创建 Session，并把数据写入平台默认
目录。可用 `SWE_AGENT_DATA_DIR` 改变数据库目录，详见
[配置参考](configuration.md)。

## 安装

仓库脚本会以 Debug 配置重新构建，并安装到 `$HOME/.local/bin/agent`：

```bash
./scripts/rebuild_install.sh
```

也可以手工指定安装前缀：

```bash
cmake --install build --prefix "$HOME/.local"
```

安装后确保 `$HOME/.local/bin` 位于 `PATH`。需要注意：当前程序仍按运行时
工作目录查找 `.env` 和 `config/agent.yaml`，安装过程不会复制这两个文件。

## 下一步

- 阅读[总体架构](architecture.md)了解组件边界。
- 阅读[Agent Loop](agent-loop.md)了解模型回复如何驱动命令。
- 遇到启动或连接问题时查阅[故障排查](troubleshooting.md)。
