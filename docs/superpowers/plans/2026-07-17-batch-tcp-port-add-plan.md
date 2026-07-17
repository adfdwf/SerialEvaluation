# Batch TCP Port Add Implementation Plan

> For agentic workers: use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Add a TCP-only batch port dialog that accepts non-contiguous port text, validates and deduplicates it, and creates the same port sessions as the existing single-port flow.

**Architecture:** Keep MainWindow::addTcpPort(quint16) as the single session-construction path. Add a small pure parser module for delimiter splitting, numeric validation, and duplicate removal; the GUI handles input, existing-session skipping, and result reporting.

**Tech Stack:** C++17, Qt 6 Core/Widgets, CMake, CTest, MSVC/Ninja.

## Global Constraints

- Batch adding is TCP-only; serial-port behavior remains unchanged.
- Input accepts comma, whitespace, semicolon, and newline separators.
- Valid ports are decimal integers in the inclusive range 1–65535.
- Duplicate input values are processed once; ports already in m_tcpSessions are skipped.
- Invalid values do not prevent other valid values from being added.
- Existing connection, send, Interval, Timeout, statistics, and log-file behavior must not change.
- Every new port must be created through the existing addTcpPort(quint16) function.

---

### Task 1: Add a pure TCP port-list parser with tests

Files:
- Create: src/tcpportparser.h
- Create: src/tcpportparser.cpp
- Create: tests/tcpportparser_test.cpp
- Modify: CMakeLists.txt

Interfaces:
- Produce TcpPortParseResult parseTcpPortList(const QString &text).
- TcpPortParseResult::ports contains unique valid quint16 ports in first-seen order.
- invalidCount counts non-empty tokens outside 1..65535 or not decimal integers.
- duplicateCount counts repeated valid tokens after their first occurrence.

- [ ] Step 1: Write the failing parser test.

Create a Qt test with these assertions:

    const auto result = parseTcpPortList(QStringLiteral("10160,10165 10200;10300\n10400"));
    QCOMPARE(result.ports, QVector<quint16>({10160, 10165, 10200, 10300, 10400}));
    QCOMPARE(result.invalidCount, 0);
    QCOMPARE(result.duplicateCount, 0);

    const auto duplicateResult = parseTcpPortList(QStringLiteral("10160,10160;10165 10160"));
    QCOMPARE(duplicateResult.ports, QVector<quint16>({10160, 10165}));
    QCOMPARE(duplicateResult.duplicateCount, 2);

    const auto invalidResult = parseTcpPortList(QStringLiteral("0,-1,65536,abc,10160"));
    QCOMPARE(invalidResult.ports, QVector<quint16>({10160}));
    QCOMPARE(invalidResult.invalidCount, 4);

    const auto boundaryResult = parseTcpPortList(QStringLiteral("1 65535"));
    QCOMPARE(boundaryResult.ports, QVector<quint16>({1, 65535}));

Use QTEST_APPLESS_MAIN(TcpPortParserTest), link to Qt6::Core, and add the parser files to the test target.

- [ ] Step 2: Run the focused test and verify RED.

    ctest --test-dir out/build/debug-clean -R TcpPortParserTest --output-on-failure

Expected: the test target is unavailable or compilation fails because the parser interface does not exist.

- [ ] Step 3: Implement the minimal parser.

Split with QRegularExpression(QStringLiteral("[,;\\s]+")) and Qt::SkipEmptyParts. Convert each token using toUInt(&ok), reject conversion failures and values outside 1..65535, and use QSet<quint16> to preserve first-seen order while counting duplicates.

- [ ] Step 4: Add parser sources and rerun tests.

Add the parser header/source to PROJECT_SOURCES, and add the parser files plus tests/tcpportparser_test.cpp to the test target. Reconfigure, build, and rerun the focused test. Expected: all parser cases pass.

- [ ] Step 5: Commit the parser change.

    git add src/tcpportparser.h src/tcpportparser.cpp tests/tcpportparser_test.cpp CMakeLists.txt
    git commit -m "test: add TCP port list parser"

---

### Task 2: Add the TCP batch-add control and GUI workflow

Files:
- Modify: src/mainwindow.h
- Modify: src/mainwindow.cpp

Interfaces:
- Add QPushButton *m_tcpBatchAddPortButton.
- Add void slotBatchAddTcpPorts().
- Consume parseTcpPortList(const QString &text).
- Produce sessions only through addTcpPort(quint16).

- [ ] Step 1: Add the button field and slot declaration.

Place the new field next to m_tcpAddPortButton, and declare the slot next to slotAddTcpPort().

- [ ] Step 2: Add the Batch Add button.

In setupTcpPortUi(), create a QPushButton with text Batch Add, add it after the existing single-port add button, and connect clicked to slotBatchAddTcpPorts.

- [ ] Step 3: Implement input parsing and result handling.

Use QInputDialog::getMultiLineText with a prompt for comma/space/semicolon-separated ports. Cancelled or empty input returns without modifying m_tcpSessions.

For each parsed port, use this flow:

    int addedCount = 0;
    int existingCount = 0;
    for (const quint16 port : result.ports) {
        if (m_tcpSessions.contains(port)) {
            ++existingCount;
            continue;
        }
        addTcpPort(port);
        ++addedCount;
    }

If there are no valid ports, show a warning. Otherwise show an information message with added, existing/skipped, duplicate, and invalid counts. The existing single-port dialog remains unchanged.

- [ ] Step 4: Keep button state aligned with test state.

Disable m_tcpBatchAddPortButton wherever m_tcpAddPortButton is disabled at TCP test start, and re-enable it wherever the single-add button is re-enabled after stop. Leave it enabled while disconnected so users can configure ports.

- [ ] Step 5: Build and manually verify.

    cmake --build out/build/debug-clean --parallel 4

Verify in TCP mode that 10160,10165 10200;10300 creates four tabs and table rows; repeated and already-existing ports are skipped; malformed tokens do not block valid ports.

- [ ] Step 6: Commit the GUI change.

    git add src/mainwindow.h src/mainwindow.cpp
    git commit -m "feat: batch add TCP ports"

---

### Task 3: Full verification and handoff

Files:
- Test only; no production files should change in this task.

- [ ] Step 1: Run all tests.

    ctest --test-dir out/build/debug-clean --output-on-failure

Expected: ProtocolFrameDecoderTest and TcpPortParserTest pass.

- [ ] Step 2: Check formatting and scope.

    git diff --check
    git status --short

Confirm no whitespace errors and only intended feature files are changed besides pre-existing user modifications.

- [ ] Step 3: Confirm behavior.

Confirm the feature is TCP-only, single-port addition still works, duplicate/invalid counts are reported, and every added port uses the existing session initialization path.

