# Task 1 Report: Lock down TCP multi-frame ordering

## Scope

Added a regression test to `tests/protocolframedecoder_test.cpp` for two concatenated 9-byte device responses:

- `A00064810000000085`
- `A00064810000000186`

The test appends both responses in one `appendData` call, verifies the decoded frames are exactly `{firstResponse, secondResponse}` in that order, and verifies that no decoder errors are produced.

## TDD evidence

The regression assertion was added before making any production-code changes. The mandated build and test run passed immediately because the existing decoder already preserves the ordering. No production code was changed; the passing initial run is recorded as baseline confirmation rather than a red-to-green implementation cycle.

## Changed files

- `tests/protocolframedecoder_test.cpp`
- `.superpowers/sdd/task-1-report.md`

## Verification

```text
cmake --build build --target ProtocolFrameDecoderTest
ctest --test-dir build -C Debug -R ^ProtocolFrameDecoderTest$ --output-on-failure
```

Results: target build completed successfully; CTest ran `ProtocolFrameDecoderTest` with 1/1 tests passing and 0 failures.

The build output also contained an existing environment warning that `pwsh.exe` was not recognized; it did not affect the build exit status or the test result.

## Self-review

- Confirmed both required response byte sequences are present verbatim.
- Confirmed both responses are appended as one TCP chunk.
- Confirmed the expected frame vector is ordered `firstResponse, secondResponse`.
- Confirmed `errors.isEmpty()` is asserted.
- Confirmed no decoder production code or unrelated working-tree changes are included in this task's staged commit.

## Review follow-up: Qt Test slot compliance

The review correctly identified that the original hand-rolled `main` did not meet the brief's requirement for a Qt Test slot. The test executable now uses a `QObject` test class with `Q_OBJECT`, `private Q_SLOTS`, and `QTEST_APPLESS_MAIN`.

`preservesOrderForConcatenatedDeviceResponses()` is the dedicated Qt Test slot and retains the required assertions:

- `QCOMPARE(result.frames, QVector<QByteArray>({first, second}))`
- `QVERIFY(result.errors.isEmpty())`

`ProtocolFrameDecoderTest` now links `Qt::Test`; the pre-link build failed with `QtTest` unavailable for this target, and after adding that dependency the focused build and CTest command passed (1/1 tests, 0 failures). The build emitted the pre-existing `pwsh.exe` environment warning, but its exit code was zero and CTest passed.

### Follow-up self-review

- Confirmed the response-order case is a real Qt Test private slot, not manual boolean aggregation.
- Confirmed the test runner is supplied by `QTEST_APPLESS_MAIN`.
- Confirmed the CMake target links the Qt Test module required for the slot and runner.
