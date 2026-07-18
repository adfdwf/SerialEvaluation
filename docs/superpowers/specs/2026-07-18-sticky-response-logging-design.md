# Sticky response logging design

## Goal

Replace the two-line sticky-response log sequence with one port-scoped ERROR entry that preserves the complete received sticky data in the request's original display format.

## Scope

Apply the behavior to TCP and serial workers. Leave timeout, connection, send scheduling, statistics, checksum-error, and normal response logging unchanged.

## Design

Workers will treat a detected sticky response as one abnormal response and pass the complete received data to the existing response path. They will not emit a separate generic "sticky packet detected" error.

For length-based responses, the reported payload is the full response buffer, including the expected response and all surplus bytes. For the TCP protocol-frame path, when one read decodes multiple frames, the frames are concatenated in receive order and reported once.

The main window will identify this abnormal response as `sticky response`, preserve its normal port and packet-id prefix, and output it with `LogLevel::Error`. It will use the outstanding packet's transmit format: HEX requests yield uppercase spaced HEX; ASCII requests yield escaped ASCII via the existing `payloadToDisplay` formatter.

Example:

```text
[ERROR] [Port 10160] #23 sticky response: A0 81 ... A0 81 ...
```

## Error handling and verification

No response timeout paths change. A sticky response still marks the outstanding packet lost and allows the existing scheduler recovery path to continue.

Tests will cover TCP and serial length-based sticky buffers, TCP multiple decoded frames, one emitted ERROR log per event, content preservation, and HEX/ASCII formatting.
