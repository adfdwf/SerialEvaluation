# Runtime port log tab switching

## Goal

During a continuous multi-port test, allow the operator to select any TCP or serial port tab and inspect its independently updating log. Keep all existing communication, scheduling, and run-time editing restrictions unchanged.

## Scope

- TCP multi-port continuous tests.
- Serial multi-port continuous tests, where the same container-level disablement currently prevents tab changes.
- The existing per-port log widget and logging pipeline.

Out of scope: changing connection behavior, send scheduling, statistics, log persistence, or the layout of the port pages.

## Design

Each port's controls already live in a `commandPage` within a `QTabWidget`. A disabled tab widget (or any disabled ancestor) prevents both page interaction and tab selection. The change separates these concerns:

- Keep the `QTabWidget` and its tab bar enabled while a test runs.
- Disable every port session's `commandPage`, preserving the existing prohibition on editing commands, changing serial settings, adding/removing commands, and issuing per-port sends.
- Keep the existing run-time disablement of port-management and global action controls.
- On test stop, restore all controls and command pages to their original enabled state.

For TCP, replace the existing `m_tcpCommandTabs` disable/enable calls with a loop over TCP sessions' command pages. For serial, do not disable `m_serialPortBox`, because it contains the tab bar; instead apply the existing disabled state to its management buttons, port table, global Send All button, and each serial command page. Restore the same set of controls on stop.

## Data flow and error handling

This is a UI-state-only change. Worker signals will continue to append to the already-selected port's `QTextEdit` through the existing log path. No worker, connection, scheduling, or error-handling path changes. Missing session/page pointers are skipped defensively.

## Verification

- Build the project and run the existing automated tests.
- In TCP mode, create at least two ports, start a continuous test, and verify switching tabs shows each port's live log while page controls remain disabled.
- Repeat the same verification for at least two serial ports where available.
- Stop the test and verify the existing controls regain their normal enabled state.
