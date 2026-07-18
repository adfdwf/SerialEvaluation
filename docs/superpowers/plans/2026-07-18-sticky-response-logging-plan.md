# Sticky Response Logging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Emit one complete, port-scoped ERROR log for each TCP or serial sticky response, formatted according to the original request.

**Architecture:** The TCP and serial workers will emit a complete sticky payload once and suppress their generic sticky-error signal. The existing main-window abnormal-response path will receive a sticky-specific reason and render that one payload using the outstanding packet's format.

**Tech Stack:** C++17, Qt Core/Widgets/Test, CMake/CTest.

## Global Constraints

- Preserve timeout, connection, scheduling, statistics, checksum-error, and normal response behavior.
- A sticky response produces one ERROR line containing its full received bytes.
- HEX requests use uppercase spaced HEX; ASCII requests use the existing escaped ASCII formatter.
- Do not modify unrelated working-tree changes.

---

## File Structure

- Modify: `src/tcpclientworker.cpp` — coalesce length and protocol-frame sticky data into one abnormal response.
- Modify: `src/serialclientworker.cpp` — coalesce a length-based sticky buffer into one abnormal response.
- Modify: `src/mainwindow.cpp` — render sticky errors as one `sticky response` event instead of a generic worker error plus `invalid/sticky response`.
- Modify: `tests/protocolframedecoder_test.cpp` — lock down multi-frame data ordering used by the TCP worker.
- Modify: `CMakeLists.txt` only if a worker-level Qt Test target is required for the new regression coverage.

### Task 1: Lock down TCP multi-frame ordering

**Files:**

- Modify: `tests/protocolframedecoder_test.cpp`

**Interfaces:**

- Consumes: `Ciqtek::ProtocolFrameDecoder::appendData(const QByteArray &)`.
- Produces: a regression test proving two complete protocol frames remain ordered in one decode result.

- [ ] **Step 1: Write the failing test**

Add a Qt Test slot that creates two valid 9-byte device responses, appends their concatenation once, and asserts `result.frames` contains exactly those two byte arrays in receive order.

```cpp
void ProtocolFrameDecoderTest::keepsMultipleResponseFramesInReceiveOrder()
{
    ProtocolFrameDecoder decoder;
    const QByteArray first = QByteArray::fromHex("A00064810000000085");
    const QByteArray second = QByteArray::fromHex("A00064810000000186");

    const auto result = decoder.appendData(first + second);

    QCOMPARE(result.frames, QVector<QByteArray>({first, second}));
    QVERIFY(result.errors.isEmpty());
}
```

- [ ] **Step 2: Run the focused test**

Run: `cmake --build build --target ProtocolFrameDecoderTest && ctest --test-dir build -C Debug -R ^ProtocolFrameDecoderTest$ --output-on-failure`

Expected before the test method is registered: the existing target passes without exercising this new case. After registration: the new case passes and documents ordered frame aggregation.

- [ ] **Step 3: Commit the regression test**

```powershell
git add tests/protocolframedecoder_test.cpp
git commit -m "test: cover sticky TCP frame ordering"
```

### Task 2: Coalesce TCP sticky input into one abnormal response

**Files:**

- Modify: `src/tcpclientworker.cpp:275-325`

**Interfaces:**

- Consumes: the current request format, `rawResponseBuffer`, and `ProtocolFrameDecoder::DecodeResult::frames`.
- Produces: one `signalDataReceived(fullStickyData, false)` event per sticky detection and no `reportError` call for that same sticky event.

- [ ] **Step 1: Update length-based TCP handling**

When `rawResponseBuffer.size() > expectedResponseBytes`, copy the full buffer before clearing it, mark it invalid, and emit the full copy:

```cpp
const QByteArray responseData = rawResponseBuffer;
const int extraBytes = responseData.size() - expectedResponseBytes;
const bool responseValid = extraBytes == 0;
rawResponseBuffer.clear();
// statistics handling remains unchanged
waitingForResponse = false;
armSendInterval(responseCompletedAt);
emit signalDataReceived(responseData, responseValid);
```

Do not call `reportError` for `extraBytes > 0`.

- [ ] **Step 2: Update protocol-frame TCP handling**

Replace the per-frame sticky emission with one aggregate emission when `result.frames.size() > 1`:

```cpp
QByteArray responseData;
for (const QByteArray &frame : result.frames) responseData.append(frame);
const bool responseValid = result.errors.isEmpty() && result.frames.size() <= 1;
// retain existing statistics behavior for each decoded frame only when responseValid
waitingForResponse = false;
armSendInterval(responseCompletedAt);
emit signalDataReceived(responseData, responseValid);
```

Retain existing `reportError` calls for decoder checksum/protocol errors; remove only the generic `TCP sticky packet detected` report.

- [ ] **Step 3: Build and run TCP-focused checks**

Run: `cmake --build build --target CommBenchPro ProtocolFrameDecoderTest && ctest --test-dir build -C Debug -R "^(ProtocolFrameDecoderTest|TcpPortParserTest)$" --output-on-failure`

Expected: both tests pass and CommBenchPro builds.

- [ ] **Step 4: Commit the TCP worker change**

```powershell
git add src/tcpclientworker.cpp
git commit -m "fix: coalesce TCP sticky response logs"
```

### Task 3: Coalesce serial sticky input and render one log entry

**Files:**

- Modify: `src/serialclientworker.cpp:261-281`
- Modify: `src/mainwindow.cpp:979-983, 1209-1213, 1446-1486, 1737-1777`

**Interfaces:**

- Consumes: full serial response buffer and the existing `handleTcpAbnormalResponse` / `handleSerialAbnormalResponse` paths.
- Produces: `sticky response: <payload>` as the reason shown in one ERROR log line, with payload rendered using the outstanding packet's recorded `txFormat`.

- [ ] **Step 1: Update serial length-based sticky handling**

Use the same full-buffer pattern as TCP:

```cpp
const QByteArray responseData = responseBuffer;
const int extraBytes = responseData.size() - expectedResponseBytes;
const bool responseValid = extraBytes == 0;
responseBuffer.clear();
// retain existing statistics handling
waitingForResponse = false;
armSendInterval(responseCompletedAt);
emit signalDataReceived(responseData, responseValid);
```

Remove only `reportError(QStringLiteral("Serial sticky packet detected..."))`.

- [ ] **Step 2: Pass a sticky-specific reason from the main window**

In both data-received lambdas, replace the invalid response reason with:

```cpp
const QString reason = !responseValid
    ? QStringLiteral("sticky response")
    : QStringLiteral("response exceeded Timeout %1 ms").arg(ui->spinBoxTimeout->value());
```

- [ ] **Step 3: Render the sticky reason with a colon**

In both abnormal-response handlers, change the lost-packet text construction so that the sticky case reads:

```cpp
QStringLiteral("%1 #%2 %3: %4")
    .arg(prefix)
    .arg(lostPacket.id)
    .arg(reason)
    .arg(payloadToDisplay(data, lostPacket.txFormat, false))
```

Keep the existing text for every non-sticky reason. Use a local `const bool sticky = reason == QStringLiteral("sticky response");` to choose the new form.

- [ ] **Step 4: Verify the full suite and manually inspect logs**

Run: `cmake --build build && ctest --test-dir build -C Debug --output-on-failure`

Manual TCP and serial checks:

1. Send one ASCII request and return that response twice in one read.
2. Confirm one ERROR line contains the full duplicated escaped ASCII response.
3. Repeat with a HEX request; confirm one ERROR line contains full uppercase spaced HEX.
4. Confirm no `sticky packet detected` or `invalid/sticky response` line accompanies the event.

Expected: one complete ERROR per sticky response, no duplicate generic sticky logs.

- [ ] **Step 5: Commit the serial/UI change**

```powershell
git add src/serialclientworker.cpp src/mainwindow.cpp
git commit -m "fix: log complete sticky responses once"
```

