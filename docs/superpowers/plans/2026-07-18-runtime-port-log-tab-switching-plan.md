# Runtime Port Log Tab Switching Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Allow switching among TCP and serial port log tabs while a continuous multi-port test is running, without allowing run-time edits or changing communication behavior.

**Architecture:** Keep each `QTabWidget` enabled so that its tab bar remains selectable. Disable each session's `commandPage` instead, and preserve the existing disablement of management, connection, and global-send controls. This is confined to start/stop UI state transitions in `MainWindow`.

**Tech Stack:** C++17, Qt Widgets, Qt Test, CMake/CTest.

## Global Constraints

- Make all code changes on the local `feat/czl` branch.
- Do not change connection, worker, send scheduling, statistics, log persistence, or log-routing behavior.
- During a running test, only tab selection may become newly available; page controls and existing management controls must remain disabled.
- Leave unrelated working-tree changes untouched.

---

## File Structure

- Modify: `src/mainwindow.cpp:1510-1573` — replace serial container-level disablement with control/page-level state transitions.
- Modify: `src/mainwindow.cpp:1770-1854` — keep the TCP tab widget enabled while disabling/re-enabling its session pages.
- Modify: `CMakeLists.txt:69-98` — register the UI-state regression test target.
- Create: `tests/portlogtabs_test.cpp` — exercise Qt tab selection with disabled content pages, the state used by both port modes.

### Task 1: Add the Qt tab-state regression test

**Files:**

- Create: `tests/portlogtabs_test.cpp`
- Modify: `CMakeLists.txt:69-98`

**Interfaces:**

- Consumes: Qt Widgets' `QTabWidget` and `QWidget` enabled-state API.
- Produces: `PortLogTabsTest`, registered with CTest as `PortLogTabsTest`.

- [ ] **Step 1: Write the regression test**

Create `tests/portlogtabs_test.cpp` with the exact test below. It expresses the required UI invariant: content pages may be disabled while the tab selector remains usable.

```cpp
#include <QTabWidget>
#include <QtTest/QtTest>

class PortLogTabsTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void switchesTabsWhenPortPagesAreDisabled();
};

void PortLogTabsTest::switchesTabsWhenPortPagesAreDisabled()
{
    QTabWidget tabs;
    auto *firstPage = new QWidget(&tabs);
    auto *secondPage = new QWidget(&tabs);
    tabs.addTab(firstPage, QStringLiteral("Port 10160"));
    tabs.addTab(secondPage, QStringLiteral("Port 10161"));

    firstPage->setEnabled(false);
    secondPage->setEnabled(false);

    QVERIFY(tabs.isEnabled());
    QVERIFY(!firstPage->isEnabled());
    QVERIFY(!secondPage->isEnabled());
    tabs.setCurrentIndex(1);
    QCOMPARE(tabs.currentIndex(), 1);
}

QTEST_MAIN(PortLogTabsTest)

#include "portlogtabs_test.moc"
```

- [ ] **Step 2: Register and run the test to verify the harness fails before registration**

Add this target inside `if(BUILD_TESTING)` in `CMakeLists.txt`, after `ResponseTimingTest`:

```cmake
    add_executable(PortLogTabsTest
        tests/portlogtabs_test.cpp
    )
    target_link_libraries(PortLogTabsTest PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME PortLogTabsTest COMMAND PortLogTabsTest)
```

Run: `cmake -S . -B build -DBUILD_TESTING=ON`

Expected before adding the target: CTest has no test named `PortLogTabsTest`. Expected after adding it: configure succeeds and CTest lists the test.

- [ ] **Step 3: Run the focused regression test**

Run: `cmake --build build --target PortLogTabsTest && ctest --test-dir build -R ^PortLogTabsTest$ --output-on-failure`

Expected: the test passes, confirming the Qt state pattern required by the fix.

- [ ] **Step 4: Commit the test harness**

```powershell
git add CMakeLists.txt tests/portlogtabs_test.cpp
git commit -m "test: cover disabled port pages with selectable tabs"
```

### Task 2: Keep TCP log tabs selectable during a continuous test

**Files:**

- Modify: `src/mainwindow.cpp:1799-1808`
- Modify: `src/mainwindow.cpp:1845-1853`

**Interfaces:**

- Consumes: `m_tcpSessions` and `TcpPortSession::commandPage`.
- Produces: a TCP start/stop UI state in which `m_tcpCommandTabs` stays enabled and every TCP command page follows the test's disabled/enabled state.

- [ ] **Step 1: Make the implementation change**

In `MainWindow::startTcpTest()`, replace:

```cpp
    m_tcpCommandTabs->setEnabled(false);
```

with:

```cpp
    for (TcpPortSession *session : m_tcpSessions) {
        if (session && session->commandPage) session->commandPage->setEnabled(false);
    }
```

In `MainWindow::stopTcpTest(bool)`, replace:

```cpp
    m_tcpCommandTabs->setEnabled(true);
```

with:

```cpp
    for (TcpPortSession *session : m_tcpSessions) {
        if (session && session->commandPage) session->commandPage->setEnabled(true);
    }
```

Do not change the existing enablement calls for TCP port management, global send, connection, or top-level Start/Stop controls.

- [ ] **Step 2: Build the application target**

Run: `cmake --build build --target CommBenchPro`

Expected: build succeeds with no compiler or linker errors.

- [ ] **Step 3: Manually verify the TCP runtime invariant**

1. Start CommBenchPro in TCP mode.
2. Add a second TCP port and connect the ports.
3. Start a continuous test.
4. Select both `Port <number>` tabs while data is flowing.
5. Confirm each page's log is visible and updates, while command entries and per-port send controls are disabled.
6. Stop the test and confirm the page controls return to their normal state.

Expected: tab changes work throughout the test; no other control becomes newly actionable.

- [ ] **Step 4: Commit the TCP fix**

```powershell
git add src/mainwindow.cpp
git commit -m "fix: keep TCP log tabs selectable during tests"
```

### Task 3: Apply the same UI-state separation to serial ports

**Files:**

- Modify: `src/mainwindow.cpp:1527-1532`
- Modify: `src/mainwindow.cpp:1569-1573`

**Interfaces:**

- Consumes: `m_serialSessions`, `SerialPortSession::commandPage`, and the existing serial management widgets.
- Produces: a serial start/stop UI state in which serial tabs remain selectable and all previously restricted controls/pages remain disabled during tests.

- [ ] **Step 1: Make the serial start-state change**

In `MainWindow::startSerialTest()`, replace:

```cpp
    if (m_serialPortBox) m_serialPortBox->setEnabled(false);
```

with:

```cpp
    if (m_serialAddPortButton) m_serialAddPortButton->setEnabled(false);
    if (m_serialRemovePortButton) m_serialRemovePortButton->setEnabled(false);
    if (m_serialRefreshButton) m_serialRefreshButton->setEnabled(false);
    if (m_serialSendAllButton) m_serialSendAllButton->setEnabled(false);
    if (m_serialPortTable) m_serialPortTable->setEnabled(false);
    for (SerialPortSession *session : m_serialSessions) {
        if (session && session->commandPage) session->commandPage->setEnabled(false);
    }
```

- [ ] **Step 2: Make the serial stop-state change**

In `MainWindow::stopSerialTest(bool)`, replace:

```cpp
    if (m_serialPortBox) m_serialPortBox->setEnabled(true);
```

with:

```cpp
    if (m_serialAddPortButton) m_serialAddPortButton->setEnabled(true);
    if (m_serialRemovePortButton) m_serialRemovePortButton->setEnabled(true);
    if (m_serialRefreshButton) m_serialRefreshButton->setEnabled(true);
    if (m_serialSendAllButton) m_serialSendAllButton->setEnabled(true);
    if (m_serialPortTable) m_serialPortTable->setEnabled(true);
    for (SerialPortSession *session : m_serialSessions) {
        if (session && session->commandPage) session->commandPage->setEnabled(true);
    }
```

- [ ] **Step 3: Run all automated checks**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`

Expected: `PortLogTabsTest`, `ProtocolFrameDecoderTest`, `TcpPortParserTest`, and `ResponseTimingTest` all pass.

- [ ] **Step 4: Manually verify serial behavior when two serial ports are available**

1. Switch to Serial Port mode and add two available ports.
2. Connect them and start a continuous test.
3. Switch between the serial tabs while log entries arrive.
4. Confirm page controls, port-management controls, table actions, and global send remain disabled.
5. Stop the test and confirm all controls return to their normal state.

Expected: serial tabs are selectable during a test, matching TCP behavior without changing serial I/O behavior.

- [ ] **Step 5: Commit the serial fix**

```powershell
git add src/mainwindow.cpp
git commit -m "fix: keep serial log tabs selectable during tests"
```

