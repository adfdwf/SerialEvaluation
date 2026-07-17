# Unmatched Response Recovery Design

## Goal

Prevent a single `Unmatched response`, late response, invalid response, or related I/O failure from permanently stopping a TCP or serial port's continuous sending.

The required behavior is:

- Log the abnormal response with the `ERROR` label.
- If a request is still pending, mark that request as `Lost`.
- Do not count the abnormal response as successful RX or RX bytes.
- Clear the port's waiting state.
- Continue with the next command when the current operation is still active.
- Keep recovery isolated to the affected port.

## Current failure

The main-window response handlers set `session->awaitingResponse` before enqueuing a send. Some error branches only append an error log. If no pending request is found, the branch can leave `awaitingResponse` set to `true`. Future calls to `sendNextTcpPacket()` or `sendNextSerialPacket()` return immediately because they are guarded by that flag.

The same class of failure can occur when a worker rejects a queued task, fails to write, or discards an oversized response buffer without delivering a completion signal to the main window.

## Design

### Unified abnormal-response handling

TCP and serial response callbacks will follow the same state transition for invalid, late, sticky, and unmatched responses:

1. Inspect the port's pending request.
2. If one exists, mark the oldest pending request as `Lost` and retain its elapsed time when available.
3. If none exists, log the response as unmatched without creating a synthetic lost request.
4. Mark the active test statistics as invalid.
5. Set `awaitingResponse` to `false`.
6. If the port is still connected and active, schedule the next one-shot or continuous command.

The implementation should centralize this repeated state transition as a small MainWindow helper or equivalent local abstraction, while preserving TCP/serial-specific session types and log prefixes.

### I/O failure recovery

The same completion invariant applies when a send cannot proceed:

- A full worker task queue must not leave the session waiting for a response that will never exist.
- A socket or serial write failure must release the waiting state and mark the active request as lost when it was already recorded.
- A response-buffer limit failure must notify the main-window state machine or otherwise perform the same lost-and-continue transition.

These paths remain errors and invalidate the affected test statistics, but they must not silently deadlock the port.

### Statistics

For each port:

- `TX Count` and `TX Bytes` count only data successfully written by the worker.
- `RX Count` and `RX Bytes` count only matched, valid responses.
- `Lost Count` increases once for a pending request that times out or fails validation.
- An unmatched response with no pending request does not create a second lost request.
- Success rate remains `successful responses / successfully sent requests * 100`.

TCP and serial statistics remain independent. An error on one port must not stop or alter another port's state.

### State guards

The recovery helper must not schedule new work when:

- the port is disconnected;
- the test or Send All operation has ended;
- the send limit has been reached and the session is only draining pending requests;
- the user has pressed Stop or disconnected all ports.

## Data flow

```text
sendNext...
  -> awaitingResponse = true
  -> enqueue worker task
  -> successful write / signalDataSent
  -> matched response: Success, clear waiting, schedule next
  -> timeout or abnormal response: Lost, ERROR, clear waiting, schedule next
  -> queue/write/buffer failure: ERROR, clear waiting, schedule next
```

## Verification

The implementation must be tested with:

1. Normal continuous TX/RX.
2. No server response, verifying Timeout is followed by another TX.
3. Unmatched response, verifying the next TX appears afterward.
4. Late response, verifying ERROR plus continued sending.
5. Sticky, malformed, or invalid response, verifying Lost accounting and continued sending.
6. Worker queue-full and write-failure paths, verifying no permanent `awaitingResponse` state.
7. Multiple ports, verifying one port's failure does not stop the others.
8. Stop and Disconnect, verifying no new work is scheduled afterward.

The primary acceptance criterion is that every recoverable abnormal event is followed by either the next `[TX]` entry or an explicit terminal state such as Stop, send-limit completion, or Disconnect.
