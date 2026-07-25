# Selected-Port Batch Control Design

## Goal

让 TCP 和串口模式支持按端口多选后批量控制连接、断开、连续发送、停止和单次批量发送；每个端口的生命周期、调度、统计和日志彼此独立。

## Current Problem

顶部 `Connect`、`Disconnect`、`Start`、`Stop` 目前作用于当前模式下的全部端口。端口表只能单选，顶部 `Send All Ports` 也会遍历全部端口。全局状态和端口状态耦合，无法只操作部分端口。

## Design

### Port selection and state

- TCP 和串口端口表使用 Qt 的扩展多选模式。
- 批量操作首先读取当前选中的端口集合。
- 未选中端口时，批量按钮禁用。
- 每个端口继续独立维护 worker、连接状态、连续测试状态、单次发送状态、发送定时器、响应等待、统计和日志。
- `m_testRunning` 表示至少一个端口处于连续测试状态，不再表示全部端口统一运行。

### Batch button behavior

- `Connect Selected`：只连接选中且未连接的端口。
- `Disconnect Selected`：只断开选中且已连接的端口。
- `Start Selected`：只启动选中且已连接、可发送的端口。
- `Stop Selected`：只停止选中且正在发送的端口。
- `Send Selected Ports`：只对选中且已连接的端口执行单次命令队列。
- 混合状态下跳过不适用端口，不影响其它端口。
- 端口页面和端口表行内的 `Send All` 继续只作用于单个端口。

### Lifecycle and scheduling

- 连接和断开操作按端口执行；连接一个端口不会创建或销毁其它端口的 worker。
- 连续测试的 `sendTimer`、`awaitingResponse`、`finishingAfterLimit` 和命令计数按端口独立运行。
- 停止单个端口只停止其 timer、补齐其 pending TX 的结束状态并生成该端口最终统计。
- 全局超时 timer 继续扫描仍处于 `testRunning` 或 `oneShotRunning` 的端口；没有任何端口运行时才停止。
- 停止或断开一个端口不会重置未选中端口的连接、发送和统计状态。

### Error handling and finalization

- 连接失败、发送失败、响应超时、协议错误和连接断开只影响当前端口。
- 停止或断开时，当前端口所有 pending TX 逐条输出带 TX 编号的 ERROR，并包含明确原因。
- 最后一个运行端口停止后，才恢复全局 Start/Connect 等界面状态。
- 现有 TX/RX/ERROR 一一对应语义保持不变；不引入跨端口匹配。

## UI and implementation boundaries

- 主要修改集中在 `src/mainwindow.cpp` 和 `src/mainwindow.h` 的端口表选择、批量入口、会话筛选、状态刷新和生命周期函数。
- `src/mainwindow.ui` 中的顶部按钮文本更新为 Selected 语义；动态端口表继续由 `mainwindow.cpp` 创建。
- TCP/串口 worker 的底层协议和 I/O 逻辑不改变，除非为端口级停止需要补充最小的 worker 控制接口。

## Verification

- 多选两个端口后，Connect/Disconnect/Start/Stop 只影响这两个端口。
- 选中连接状态混合的端口时，Connect 只连接未连接端口，Disconnect 只断开已连接端口。
- 停止一个端口时，另一个正在发送的端口继续发送和接收。
- 顶部 Send Selected 只对选中端口发送。
- 无选中端口时，所有批量按钮不可用。
- 全量运行 MSVC Debug 构建和现有测试。
