# Unmatched Response Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ensure an unmatched, late, invalid, sticky, timeout, or send-side failure cannot leave a TCP or serial port permanently blocked in `awaitingResponse`.

**Architecture:** Keep the existing one-worker-per-port architecture. Add narrowly scoped MainWindow recovery helpers for TCP and serial sessions, and add explicit Worker failure signals for failures that currently have no response-completion signal. Every recoverable failure will mark the pending request lost when present, clear the session wait state, log `ERROR`, and schedule the next command only when the session is still active.

**Tech Stack:** C++17, Qt 6/5, Qt signals, `StatisticsManager`, CMake/CTest, Visual Studio/MSVC.

## Global Constraints

- Preserve independent TCP and serial per-port statistics.
- Unmatched, late, invalid, sticky, and timeout responses must not increase successful RX count or RX bytes.
- A pending request is marked `Lost` at most once.
- Do not schedule new work after Stop, Disconnect, send-limit completion, or port removal.
- Do not push changes to any remote repository.

---

## File Map

- Modify `src/mainwindow.h`: declare the TCP/serial abnormal-response recovery helpers and send-failure handlers.
- Modify `src/mainwindow.cpp`: route unmatched/invalid/late responses and worker failures through the helpers; preserve per-port scheduling and statistics rules.
- Modify `src/tcpclientworker.h`: expose explicit signals for a rejected send task, failed write, and aborted response wait.
- Modify `src/tcpclientworker.cpp`: emit those signals at the exact failure points and ensure the worker does not leave an internal wait active after an abort.
- Modify `src/serialclientworker.h`: expose the same failure signal contract as the TCP worker.
- Modify `src/serialclientworker.cpp`: emit the serial equivalents at queue, write, and response-buffer failure points.
- Modify `tests/protocolframedecoder_test.cpp`: verify lost accounting remains separate from successful RX accounting.

## Interfaces

The MainWindow helpers will use these concrete signatures:

```cpp
void handleTcpAbnormalResponse(TcpPortSession *session,
                               const QByteArray &data,
                               const QString &reason,
                               qint64 elapsedMs);
void handleSerialAbnormalResponse(SerialPortSession *session,
                                  const QByteArray &data,
                                  const QString &reason,
                                  qint64 elapsedMs);
void handleTcpSendFailure(TcpPortSession *session, const QString &reason);
void handleSerialSendFailure(SerialPortSession *session, const QString &reason);
```

The Worker signals will be:

```cpp
void signalSendFailed(const QString &message);
void signalResponseAborted(const QString &message);
```

`signalSendFailed` is emitted when a queued send is discarded or the actual write fails. `signalResponseAborted` is emitted when the Worker abandons a response wait because its receive buffer limit was exceeded. Existing `signalErrorOccurred` remains for general diagnostics and connection errors; it will not be used as a state-machine completion signal because its messages are not a safe protocol for deciding whether a send was attempted.

### Task 1: Add statistics regression coverage

**Files:**
- Modify: `tests/protocolframedecoder_test.cpp`
- Test: existing `ProtocolFrameDecoderTest` executable

**Interfaces:**
- Consumes: `StatisticsManager::recordSend`, `markOldestPendingLost`, and `snapshot`.
- Produces: executable assertions proving a failed response is counted as lost and excluded from RX success/bytes.

- [ ] **Step 1: Add a focused lost-after-invalid-response assertion**

Add a second request to the existing `lostStatistics` block, then mark only the pending request lost and assert:

```cpp
lostStatistics.recordSend(QByteArray("5678"), QStringLiteral("ASCII"));
PacketInfo invalidPacket;
assert(lostStatistics.markOldestPendingLost(42, &invalidPacket));
const auto invalidResult = lostStatistics.snapshot();
assert(invalidPacket.status == PacketInfo::Status::Timeout);
assert(invalidResult.totalSent == 2);
assert(invalidResult.successReceived == 0);
assert(invalidResult.lostPackets == 2);
assert(invalidResult.totalReceivedBytes == 0);
```

- [ ] **Step 2: Build and run the focused regression test**

Run:

```powershell
cmake --build out/build/debug --config Debug --target ProtocolFrameDecoderTest --parallel 4
ctest --test-dir out/build/debug -C Debug -R ProtocolFrameDecoderTest --output-on-failure
```

Expected: the test continues to pass because this task locks down existing statistics behavior; if the configured MSVC environment reports missing standard headers such as `type_traits`, record that environment failure and do not treat it as a product failure.

- [ ] **Step 3: Commit the test coverage**

```powershell
git add tests/protocolframedecoder_test.cpp
git commit -m "test: cover unmatched response lost accounting"
```

### Task 2: Add explicit TCP Worker failure signals

**Files:**
- Modify: `src/tcpclientworker.h`
- Modify: `src/tcpclientworker.cpp`

**Interfaces:**
- Consumes: existing `enqueue`, TCP write loop, and response-buffer limit handling.
- Produces: `signalSendFailed(QString)` and `signalResponseAborted(QString)` events delivered to MainWindow.

- [ ] **Step 1: Declare the two signals**

Add beside the existing Worker signals:

```cpp
void signalSendFailed(const QString &message);
void signalResponseAborted(const QString &message);
```

- [ ] **Step 2: Emit `signalSendFailed` when the task queue rejects a send**

In `enqueue`, retain the existing error diagnostic and additionally emit the dedicated signal only when `task.type == TaskType::Send`. Capture the task type before moving the task into the queue so the decision remains valid after `std::move`.

The queue-full path must produce both an error log signal and a send-failure completion signal; it must not emit `signalDataSent`.

- [ ] **Step 3: Emit `signalSendFailed` after a TCP write failure**

In the `!writeOk` branch, emit `signalSendFailed(errorMessage)` after the failure message is constructed. Keep the existing internal pending-statistics cleanup and `signalErrorOccurred` diagnostic.

- [ ] **Step 4: Emit `signalResponseAborted` on response-buffer overflow**

In the `kMaxResponseBufferBytes` branch, emit `signalResponseAborted(QStringLiteral("TCP response buffer limit reached; response discarded"))` after clearing the receive state. Keep the existing error diagnostic, but do not emit a receive-success signal for the discarded response.

- [ ] **Step 5: Build the TCP application target**

Run:

```powershell
cmake --build out/build/debug --config Debug --target CommBenchPro --parallel 4
```

Expected: compile succeeds. If the environment fails before compiling project code because MSVC cannot find `type_traits`, fix the VS developer-environment selection before interpreting the result.

- [ ] **Step 6: Commit the TCP Worker changes**

```powershell
git add src/tcpclientworker.h src/tcpclientworker.cpp
git commit -m "fix: report TCP send and response abort failures"
```

### Task 3: Add the same explicit failure contract to the serial Worker

**Files:**
- Modify: `src/serialclientworker.h`
- Modify: `src/serialclientworker.cpp`

**Interfaces:**
- Consumes: existing serial queue, write, and response-buffer paths.
- Produces: the same `signalSendFailed(QString)` and `signalResponseAborted(QString)` contract as TCP.

- [ ] **Step 1: Declare the two serial signals**

Add the same declarations to `SerialClientWorker`:

```cpp
void signalSendFailed(const QString &message);
void signalResponseAborted(const QString &message);
```

- [ ] **Step 2: Emit the send-failure signal for queue-full and write-failure paths**

Use the same rule as TCP: queue rejection and failed serial writes emit `signalSendFailed`, while `signalErrorOccurred` remains the diagnostic channel.

- [ ] **Step 3: Emit the response-aborted signal for serial buffer overflow**

Emit `signalResponseAborted` after clearing `responseBuffer` and setting `waitingForResponse = false`.

- [ ] **Step 4: Build the serial application target**

Run:

```powershell
cmake --build out/build/debug --config Debug --target CommBenchPro --parallel 4
```

Expected: compile succeeds.

- [ ] **Step 5: Commit the serial Worker changes**

```powershell
git add src/serialclientworker.h src/serialclientworker.cpp
git commit -m "fix: report serial send and response abort failures"
```

### Task 4: Centralize TCP and serial abnormal-response recovery in MainWindow

**Files:**
- Modify: `src/mainwindow.h`
- Modify: `src/mainwindow.cpp`

**Interfaces:**
- Consumes: Worker failure signals from Tasks 2 and 3, existing `StatisticsManager` APIs, and existing one-shot/continuous scheduling methods.
- Produces: a recovery transition that always clears `awaitingResponse` and schedules only when the session is active.

- [ ] **Step 1: Declare the four recovery helpers**

Add the exact signatures from the Interfaces section to the private MainWindow methods.

- [ ] **Step 2: Implement TCP abnormal-response recovery**

The helper must:

```cpp
if (!session) return;
qint64 effectiveElapsed = elapsedMs;
if (effectiveElapsed < 0) session->statistics.oldestPendingElapsed(&effectiveElapsed);
PacketInfo lostPacket;
const bool lost = session->statistics.hasPendingPackets() &&
                  session->statistics.markOldestPendingLost(effectiveElapsed, &lostPacket);
session->awaitingResponse = false;
if (session->testRunning) session->statisticsValid = false;
appendLog(LogLevel::Error, ...);
if (session->oneShotRunning && !session->oneShotCommands.isEmpty()) {
    scheduleNextOneShotTcpPacket(session, ui->spinBoxInterval->value());
} else if (session->testRunning && !session->finishingAfterLimit) {
    scheduleNextTcpPacket(session, ui->spinBoxInterval->value());
}
```

Use the lost packet ID in the log when a pending packet was found; otherwise log `Unmatched or late response` without fabricating an ID. Never call `recordReceive` for this path.

- [ ] **Step 3: Implement the serial equivalent**

Copy the same state transition for `SerialPortSession`, preserving the serial prefix and calling the serial scheduling methods. Keep statistics and state entirely within the serial session.

- [ ] **Step 4: Route both existing invalid-response branches through the helpers**

Replace the TCP and serial `if (!responseValid || responseTimedOut)` bodies and the no-pending unmatched handling with the corresponding helper. Ensure the valid matched-response branch remains responsible only for `recordReceive`, success logging, and normal scheduling.

The no-pending valid-response branch must also clear `awaitingResponse` before attempting to schedule, because a response can arrive after the Worker has already timed out its internal request.

- [ ] **Step 5: Connect Worker failure signals**

In each TCP session setup, connect `signalSendFailed` and `signalResponseAborted` with lambdas that verify both the session lookup and Worker pointer identity, then call the appropriate helper. Do the same for every serial session.

For send failures, use an elapsed value of `-1` so the helper derives it from the session's pending request when present. For response-aborted failures, use the current oldest pending elapsed time if available.

- [ ] **Step 6: Prevent recovery after terminal states**

The helper's scheduling guards must check `session->connected`, `session->testRunning`, `session->oneShotRunning`, `session->finishingAfterLimit`, and the command queue state before scheduling. Stop and Disconnect already clear the active flags; the helper must observe those flags and do nothing after termination.

- [ ] **Step 7: Build and run all configured tests**

Run:

```powershell
cmake --build out/build/debug --config Debug --parallel 4
ctest --test-dir out/build/debug -C Debug --output-on-failure
```

Expected: all configured tests pass and `CommBenchPro` links successfully.

- [ ] **Step 8: Commit the MainWindow recovery changes**

```powershell
git add src/mainwindow.h src/mainwindow.cpp
git commit -m "fix: recover sending after unmatched responses"
```

### Task 5: Perform runtime acceptance checks without pushing

**Files:**
- No source changes unless a failing acceptance check identifies a concrete defect.
- Inspect: generated `Log/*.log` files and the application UI.

**Interfaces:**
- Consumes: the built `CommBenchPro` executable and a test TCP/serial endpoint capable of producing delayed, invalid, and unmatched responses.
- Produces: evidence that abnormal responses are followed by the next TX or an explicit terminal state.

- [ ] **Step 1: Verify normal continuous operation**

Run one connected port with a responsive endpoint. Confirm the log alternates TX/RX and the test remains active for at least 10 minutes.

- [ ] **Step 2: Verify an unmatched response resumes sending**

Inject a response when no pending request exists or inject an invalid response for the current request. Confirm the sequence contains `ERROR ... Unmatched` followed by a new `[TX]` for the same port, and that RX success/bytes do not increase for the abnormal response.

- [ ] **Step 3: Verify a timeout resumes sending**

Suppress one response. Confirm `recv timeout/lost` is logged, the request is counted as Lost, and the next TX appears.

- [ ] **Step 4: Verify multiple-port isolation**

Run at least two ports, inject an abnormal response on one, and confirm only that port records Lost while the other continues normal TX/RX.

- [ ] **Step 5: Verify Stop and Disconnect terminal behavior**

Press Stop or Disconnect during recovery and confirm no TX is logged after the terminal action.

- [ ] **Step 6: Record local verification state**

Run:

```powershell
git status -sb
git log --oneline -8
```

Expected: all intended changes are committed locally, no remote push is performed, and the branch remains on `czl`.
