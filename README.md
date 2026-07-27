# CommBenchPro

CommBenchPro 是一个基于 Qt 的串口与 TCP 通信测试工具，面向多端口并发通信、请求/响应时序验证和性能数据采集场景。

## 功能特性

- 支持 TCP 多端口连接与串口通信。
- 支持动态添加、删除和刷新通信端口。
- 支持为每个端口配置命令并执行单次 `Send All` 或连续测试。
- 支持 ASCII 与 HEX 形式的报文/响应显示。
- 对发送、响应、超时和异常事件进行关联处理，保持请求与响应的一一对应关系。
- 为每个端口提供独立的运行日志与统计信息。
- 支持记录端口参数、测试事件和最终统计结果到本地日志文件。
- 内置协议帧解析、响应处理、端口解析、响应时序和日志相关测试。

## 技术栈

- C++17
- CMake 3.16 或更高版本
- Qt 5 或 Qt 6
- Qt Widgets、Network、SerialPort、Test 模块

## 环境要求

请提前安装：

1. CMake 3.16+
2. 支持 C++17 的编译器
3. Qt 5 或 Qt 6，并确保 Qt 安装目录通过 `QTDIR` 或 `CMAKE_PREFIX_PATH` 可被 CMake 找到

`CMakeUserPresets.json` 是本地用户配置文件，已被 Git 忽略。可以在其中保存本机 Qt 路径和 IDE 相关设置。

## 构建

### Windows PowerShell

```powershell
$env:QTDIR = "D:/path/to/Qt/6.x.x/msvc2022_64"

cmake -S . -B out/build -DCMAKE_PREFIX_PATH=$env:QTDIR -DBUILD_TESTING=ON
cmake --build out/build --config Release
```

如果使用 Ninja 或其他单配置生成器，可以省略 `--config Release`。

构建完成后，可执行文件通常位于：

```text
out/build/CommBenchPro.exe
```

具体输出位置取决于所选生成器和构建配置。

## 运行测试

```powershell
ctest --test-dir out/build -C Release --output-on-failure
```

项目当前包含以下测试目标：

- `ProtocolFrameDecoderTest`：协议帧解析
- `TcpPortParserTest`：TCP 端口解析
- `ResponseTimingTest`：响应时序计算
- `ResponseHandlingTest`：响应处理
- `LogFileTest`：日志文件行为

## 使用说明

1. 启动 `CommBenchPro`。
2. 选择 TCP 或串口通信模式。
3. 添加一个或多个 TCP 端口，或刷新并添加可用串口。
4. 在对应端口的命令页中配置待发送命令及测试参数。
5. 建立连接后执行单次发送或连续测试。
6. 在各端口独立的日志区域和统计面板中查看运行结果；测试日志同时会写入本地文件。

串口测试需要根据实际设备配置端口、波特率及其他串口参数；TCP 测试需要确保目标主机和端口可访问。

## 项目结构

```text
.
├── CMakeLists.txt              # 构建配置和测试目标
├── CMakePresets.json           # 共享 CMake 预设
├── CMakeUserPresets.json       # 本地用户预设（不提交）
├── src/
│   ├── main.cpp                # 程序入口
│   ├── mainwindow.*            # 主界面、端口会话和测试调度
│   ├── serialclientworker.*    # 串口通信 worker
│   ├── tcpclientworker.*       # TCP 通信 worker
│   ├── protocolframedecoder.*  # 协议帧解析
│   ├── responsehandling.*      # 响应处理
│   ├── responsetiming.h         # 响应时序
│   ├── statisticsmanager.*     # 统计数据管理
│   └── tcpportparser.*          # TCP 端口解析
└── tests/                       # Qt 单元测试
```

## 版本控制说明

- `docs/` 用于本地设计与计划记录，不提交到远程仓库。
- `out/`、`build/` 和 Visual Studio 生成目录属于本地构建产物，不提交到远程仓库。
- 请勿将包含本机路径的用户配置直接提交；共享构建配置应修改 `CMakePresets.json`。
