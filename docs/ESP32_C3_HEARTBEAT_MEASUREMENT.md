# ESP32-C3 heartbeat TLS measurement protocol

Last updated: 2026-09-14

This document defines the measurement step that must be completed before adding a total-free-heap guard to the ESP32-C3 Minis heartbeat path.

The authoritative design constraints and previously rejected approaches remain documented in `ESP32_C3_MEMORY_NOTES.md`.

## Purpose

Determine from real runtime data whether total free heap, together with the existing largest-free-block measurement, can reliably predict when a short-lived Minis heartbeat TLS handshake is likely to fail.

This step intentionally does **not** change heartbeat thresholds, SSH buffers, ring buffers, config-fetch recovery, or SSH reconnect behaviour.

The current accepted priority remains:

1. keep the SSH session alive;
2. keep an active forwarded channel alive;
3. avoid dropped payload;
4. deliver heartbeat only when memory permits it safely.

A failed or deferred heartbeat is acceptable. A heartbeat must not intentionally disconnect a healthy SSH tunnel or active forwarded channel.

## Build under test

Use only:

```text
esp32_c3_lowmem
```

Do not erase flash for this test.

Normal commands:

```powershell
pio run -e esp32_c3_lowmem
pio run -e esp32_c3_lowmem -t upload
pio device monitor -e esp32_c3_lowmem --baud 115200
```

## Existing instrumentation

No extra runtime instrumentation is required for the first measurement round.

The current firmware already reports the required memory values around heartbeat TLS:

```text
TLS heap heartbeat-before: free=<bytes> largest=<bytes> min=<bytes>
TLS heap heartbeat-connected: free=<bytes> largest=<bytes> min=<bytes>
```

or, when the connection attempt fails:

```text
TLS heap heartbeat-connect-failed: free=<bytes> largest=<bytes> min=<bytes>
```

The normal statistics output also reports:

```text
Tunnel State: Connected
Active Channels: 0|1
Bytes Sent: ...
Bytes Received: ...
Bytes Dropped: ...
Free Heap: ... (min: ..., largest: ...)
```

These values are sufficient to correlate heartbeat success or failure with both total free heap and the largest free block without changing the memory-sensitive runtime path.

## Test sequence

Run the same flashed firmware through the following three states. Do not reboot between states unless the tunnel itself becomes unusable.

### A. SSH connected, no forwarded channel

Wait until the main SSH tunnel is connected and `Active Channels: 0`.

Capture at least three heartbeat attempts if practical.

For every attempt record:

- total free heap immediately before heartbeat TLS;
- largest free block immediately before heartbeat TLS;
- minimum free heap;
- heartbeat result / HTTP status;
- SSH tunnel state after the attempt;
- active channel count after the attempt;
- `Bytes Dropped`.

### B. One real forwarded channel active

Open one real forwarded connection and generate actual traffic through it.

Verify first:

```text
Tunnel State: Connected
Active Channels: 1
Bytes Dropped: 0
```

Keep the forwarded connection active across one or more scheduled heartbeat attempts.

For every heartbeat attempt record the same fields as in state A.

A TLS error such as:

```text
SSL - Memory allocation failed
```

is acceptable during this phase **only** when all of the following remain true:

```text
Tunnel State: Connected
Active Channels: 1
Bytes Dropped: 0
```

and forwarded traffic continues to work.

### C. Close only the forwarded channel

Close the forwarded client/channel without rebooting and without intentionally disconnecting the main SSH session.

Verify that:

```text
Tunnel State: Connected
Active Channels: 0
```

Then capture subsequent heartbeat attempts and confirm whether free heap recovers and TLS succeeds again.

## Measurement table

Record observations in this form:

| State | Attempt | Free before | Largest before | Min heap | Heartbeat result | Active channels after | Tunnel after | Bytes dropped |
| --- | ---: | ---: | ---: | ---: | --- | ---: | --- | ---: |
| Idle SSH | 1 | | | | | 0 | Connected | 0 |
| Idle SSH | 2 | | | | | 0 | Connected | 0 |
| Active channel | 1 | | | | | 1 | Connected | 0 |
| Active channel | 2 | | | | | 1 | Connected | 0 |
| Channel closed | 1 | | | | | 0 | Connected | 0 |
| Channel closed | 2 | | | | | 0 | Connected | 0 |

More samples are preferable when they occur naturally, but do not alter the heartbeat interval merely to generate more data unless that is a separate deliberate experiment.

## What this test must answer

The important question is not merely whether `largest free block` is large enough.

The already validated baseline showed approximately:

```text
free=61-63 KB
largest=32756 bytes
```

with an active forwarded channel, while a new heartbeat TLS handshake could still fail from memory pressure.

The test must therefore determine whether successful and failed attempts show a useful separation in **total free heap** while keeping the existing largest-block value visible.

Do not choose a total-heap threshold from a single observation.

## Decision rule after measurement

Only after collecting real samples should a code change be considered.

If the measurements show a repeatable memory range in which heartbeat TLS failure is predictable, the preferred future behaviour is:

```text
free heap too low for reliable heartbeat TLS
    -> defer heartbeat before creating WiFiClientSecure/TLS state
    -> leave SSH session untouched
    -> leave active forwarded channel untouched
```

Any proposed threshold must include a safety margin derived from the measured successful and failed attempts.

If the results overlap too much to choose a reliable threshold, do not add a guessed total-heap guard. Keep the current failure-safe behaviour and investigate further instead.

## Pass criteria for the baseline

This measurement round is considered safe when, throughout states A through C:

- the main SSH session remains connected except for unrelated failures;
- an active forwarded channel survives heartbeat memory pressure;
- real forwarded traffic continues during the active-channel test;
- SSH keepalive continues normally;
- `Bytes Dropped` remains `0`;
- closing the forwarded channel releases memory again;
- a later heartbeat can succeed again when sufficient memory returns.

Build success alone does not satisfy this test. Runtime logs are the evidence.
