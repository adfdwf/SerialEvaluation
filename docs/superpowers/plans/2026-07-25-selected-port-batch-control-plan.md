# Selected-Port Batch Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Connect, Disconnect, Start, Stop, and batch Send operate only on selected TCP/serial ports while preserving independent per-port lifecycles.

**Architecture:** Keep `TcpPortSession` and `SerialPortSession` as the ownership boundary for workers, timers, statistics, and logs. Add selection-to-session helpers in `MainWindow`; batch entry points filter selected sessions and invoke existing per-session operations. Replace global “all sessions” assumptions with aggregate predicates that report whether any session remains connected or running.

**Tech Stack:** C++17, Qt 6 Widgets, Qt Network, Qt SerialPort, Qt Test, CMake, MSVC Debug.

## Global Constraints

- TCP and serial workers keep their existing protocol and I/O behavior.
- A batch operation skips selected ports for which that operation is not applicable.
- An unselected port must not have its worker, timer, statistics, or command counters reset by a selected-port operation.
- Existing row-level and port-page `Send All` actions remain single-port actions.
- Validate with the MSVC Debug build in `out/build/debug-msvc` and the full CTest suite.

---

### Task 1: Add selected-session lookup and selection-aware button state

**Files:**
- Modify: `src/mainwindow.h` — declare selected TCP/serial session helpers and aggregate state helpers.
- Modify: `src/mainwindow.cpp` — switch both port tables to extended selection, update button labels, and refresh enabled states whenever selection or session state changes.
- Modify: `src/mainwindow.ui` — update static button text from all-port wording to selected-port wording where applicable.

**Interfaces:**
- Produce `QVector<TcpPortSession *> selectedTcpSessions() const` and `QVector<SerialPortSession *> selectedSerialSessions() const`.
- Produce aggregate predicates `hasRunningSessions()`, `hasConnectedSessions()`, `hasSelectedConnectableSessions()`, `hasSelectedDisconnectableSessions()`, `hasSelectedStartableSessions()`, `hasSelectedStoppableSessions()`, and `hasSelectedSendableSessions()` for UI refresh.

- [ ] **Step 1: Add failing selection behavior checks** around the new selection helpers, and assert that selected table rows map to only their corresponding session objects while an empty selection maps to an empty list.
- [ ] **Step 2: Run the focused test/build command** and confirm the new selection assertions fail before the helper implementation exists.
- [ ] **Step 3: Implement `ExtendedSelection` and lookup helpers** for both tables; use the port/name in column 0 to resolve sessions, ignore invalid rows, and call the button-state refresh after selection changes and state callbacks.
- [ ] **Step 4: Update button text and enablement** to `Connect Selected`, `Disconnect Selected`, `Start Selected`, `Stop Selected`, and `Send Selected Ports`; disable batch actions when no selected session is applicable.
- [ ] **Step 5: Build the affected Debug target** with `cmake --build out/build/debug-msvc --config Debug --parallel 4` and confirm it compiles.

### Task 2: Make Connect and Disconnect operate on selected sessions

**Files:**
- Modify: `src/mainwindow.h` — update connection method contracts or add per-session connection helpers.
- Modify: `src/mainwindow.cpp` — refactor `connectTcpPorts`, `disconnectTcpPorts`, `connectSerialPorts`, `disconnectSerialPorts`, and worker-destruction helpers to accept/iterate selected sessions without touching unselected sessions.

**Interfaces:**
- Selected connection entry points consume the session vectors from Task 1.
- Existing worker signal callbacks continue to update only the session captured by that worker.

- [ ] **Step 1: Add a regression test or deterministic helper check** proving a mixed selection only produces connect/disconnect actions for applicable selected sessions.
- [ ] **Step 2: Run it and verify it fails** against the current all-session loops.
- [ ] **Step 3: Implement selected-only connection**; do not call a global worker-destruction routine from `Connect Selected`, and skip already connected sessions.
- [ ] **Step 4: Implement selected-only disconnection**; stop and release only selected workers, skip disconnected sessions, and preserve unselected workers and state.
- [ ] **Step 5: Recalculate aggregate connection state** from all sessions after each operation and verify the affected Debug target builds.

### Task 3: Make Start and Stop operate independently per port

**Files:**
- Modify: `src/mainwindow.cpp` — refactor `startTcpTest`, `stopTcpTest`, `startSerialTest`, `stopSerialTest`, and related scheduling/finish callbacks.
- Modify: `src/mainwindow.h` — expose selected-session start/stop contracts if signatures change.

**Interfaces:**
- Start and stop consume only selected session vectors.
- Existing `sendNextTcpPacket`, `sendNextSerialPacket`, `checkTcpTimeouts`, and `checkSerialTimeouts` remain per-session operations.

- [ ] **Step 1: Add a regression test/helper check** for stopping one selected running session while an unselected session remains running.
- [ ] **Step 2: Run it and verify the current global stop behavior fails the check.**
- [ ] **Step 3: Initialize continuous-test fields only on selected, connected sessions** and start their first sends; leave unselected sessions untouched.
- [ ] **Step 4: Stop only selected running sessions**, stop only their timers, mark their pending TX records with detailed errors, and emit their final statistics without changing unselected sessions.
- [ ] **Step 5: Change `m_testRunning` and timeout-timer handling** to aggregate over all sessions; stop the global timeout timer only when no TCP/serial session remains active.
- [ ] **Step 6: Verify selected stop/start behavior through the Debug build and focused tests.**

### Task 4: Restrict batch Send Selected to selected connected ports

**Files:**
- Modify: `src/mainwindow.cpp` — change `slotSendAllTcpPorts` and `slotSendAllSerialPorts` to use selected sessions and update the toolbar labels.
- Modify: `src/mainwindow.h` — update method comments/contracts to describe selected scope.

**Interfaces:**
- Batch send consumes selected session vectors and invokes existing `sendAllTcpPort`/`sendAllSerialPort` only for connected sessions.

- [ ] **Step 1: Add a regression test/helper check** that a selected subset receives the one-shot command and an unselected connected session does not.
- [ ] **Step 2: Run it and verify the current all-session implementation fails.**
- [ ] **Step 3: Implement selected-only batch send** and keep page-level and row-level Send All behavior unchanged.
- [ ] **Step 4: Build the full Debug project** and verify the selected batch send path compiles.

### Task 5: Verify mixed-state behavior and finalize

**Files:**
- Modify: `tests/` only if the selected-session helper seam has unit coverage.
- Modify: `src/mainwindow.cpp` comments for selected-scope behavior and any exact UI text that remains inconsistent.

- [ ] **Step 1: Run the full MSVC Debug build:** `cmake --build out/build/debug-msvc --config Debug --parallel 4`.
- [ ] **Step 2: Run the full test suite:** set `D:\Softwares\QT\6.8.3\msvc2022_64\bin` on `PATH`, then run `ctest --test-dir out\build\debug-msvc -C Debug --output-on-failure`.
- [ ] **Step 3: Manually verify two TCP and two serial sessions:** select a subset, connect/disconnect selected, start/stop selected, send selected, test mixed connected/running states, and confirm unselected logs and counters remain unchanged.
- [ ] **Step 4: Verify no batch action still contains an unconditional loop over all sessions unless it is explicitly computing aggregate UI state.**
- [ ] **Step 5: Run `git diff --check` and inspect `git status --short` before committing the implementation.**
