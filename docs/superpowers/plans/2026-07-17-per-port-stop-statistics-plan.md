# Per-Port Stop Statistics and Error Logging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ensure timeout and sticky/packet-loss events are logged as `ERROR`, while manual Stop always emits independent final statistics for every port.

**Architecture:** Keep the existing per-port `StatisticsManager` and Worker statistics. Treat manual Stop as a normal finalization event that converts pending requests to losses without suppressing the snapshot. Preserve the existing Worker error signal path and add/verify port-qualified error messages at the MainWindow boundary.

**Tech Stack:** Qt 6, C++17, CMake, CTest.

## Global Constraints

- TCP and serial ports must remain independently tracked.
- A failure on one port must not hide final statistics for other ports.
- Manual Stop must count outstanding requests as lost for that port.
- Timeout and sticky-packet events must use `LogLevel::Error`.

---

### Task 1: Make final snapshots available after manual Stop

**Files:**
- Modify: `src/tcpclientworker.cpp:46-53`
- Modify: `src/serialclientworker.cpp:43-50`
- Modify: `src/mainwindow.cpp:1674-1687, 1438-1452`

**Interfaces:**
- Preserve `finalStatisticsSnapshot(StatisticsSnapshot *) -> bool`.
- Return the snapshot after pending requests are marked lost; use the boolean only to indicate degraded statistics quality.

- [ ] Change both Worker snapshot methods so `markAllPendingLost()` does not make the snapshot unavailable; copy the snapshot whenever the pointer is valid, and return the existing validity flag.
- [ ] Remove the MainWindow precondition that requires `session->statisticsValid` before calling the Worker snapshot method.
- [ ] Log each port's final counters independently even when another port has invalid statistics.
- [ ] Keep a separate `ERROR` message when a port's returned validity flag is false.

### Task 2: Verify error-level logging and regression behavior

**Files:**
- Inspect: `src/tcpclientworker.cpp:279-317`
- Inspect: `src/serialclientworker.cpp:258-279`
- Inspect: `src/mainwindow.cpp:1002-1013, 1199-1210`

- [ ] Confirm timeout callbacks call `appendLog(LogLevel::Error, ...)` with the port identifier.
- [ ] Confirm Worker sticky-packet/error callbacks emit `signalErrorOccurred`, which MainWindow handles as `LogLevel::Error` with the port identifier.
- [ ] Build the application with the existing Visual Studio/CMake command.
- [ ] Run the complete CTest suite and verify every test passes.
- [ ] Check `git diff --check` and confirm no unrelated files changed.
