# Task 2 Report: Coalesce TCP Sticky Input

## Scope

- Modified `src/tcpclientworker.cpp` only for production behavior.
- Added this required task report.

## Implementation

- Length-based responses now retain and emit the full raw buffer once. A buffer longer than the expected response is emitted with `responseValid == false` and does not report the generic TCP sticky-packet error.
- Protocol-frame responses now concatenate all frames decoded in one append, preserving receive order, and emit one event. Multiple frames make that event invalid; decoder checksum and protocol errors continue to use `reportError`.
- Normal single-response statistics and scheduling paths are unchanged. Invalid aggregate responses mark the oldest pending request lost once before the single emission.

## TDD Evidence

The task brief restricted implementation changes to `src/tcpclientworker.cpp`, while the pre-existing `ProtocolFrameDecoderTest` already covers multi-frame receive ordering (commit `718c97f`). No additional test file was added to preserve the requested task scope. The focused regression suite passed before and after the production change.

## Verification

Commands run after the change:

```powershell
cmake --build build --target CommBenchPro ProtocolFrameDecoderTest --config Debug
ctest --test-dir build -C Debug -R '^(ProtocolFrameDecoderTest|TcpPortParserTest)$' --output-on-failure
```

Results:

- `CommBenchPro` built successfully.
- `ProtocolFrameDecoderTest` built successfully and passed.
- `TcpPortParserTest` passed.
- CTest summary: 2/2 tests passed.
- `git diff --check` passed with no whitespace errors.

The CommBenchPro build output included a non-fatal, pre-existing environment message that `pwsh.exe` was not found; CMake/MSBuild still exited successfully and produced `build/Debug/CommBenchPro.exe`.

## Self-review

- Verified no `TCP sticky packet detected` report remains in `src/tcpclientworker.cpp`.
- Verified length-based sticky data is copied before the raw buffer is cleared.
- Verified protocol frames are appended in decoder output order and emitted once.
- Verified decoder errors remain reported, while sticky aggregation itself does not produce a generic error.
- Verified no unrelated working-tree files are staged for this task.
