# ESP32-C3 heartbeat TLS measurement record

Last updated: 2026-09-15

This document records the measurement plan that was used to determine whether
total free heap should be considered together with the largest free block
before starting Minis TLS on the ESP32-C3.

**Status: completed / historical.**

The measurement phase described here has already been performed. The resulting
runtime decisions and latest measured values are maintained in
`ESP32_C3_MEMORY_NOTES.md`, which is now the authoritative source for current
memory thresholds and control-plane behaviour.

Do not use this file as a current implementation guide.

## What the measurement established

Earlier firmware guarded Minis TLS mainly by largest free block. Real hardware
showed that this was insufficient: an active forwarded channel could have a
largest free block around 32 KB while total free heap was only about 61-63 KB,
and mbedTLS could still fail from memory pressure.

The preferred behaviour was therefore validated as:

```text
insufficient memory for Minis TLS
    -> do not disturb SSH
    -> defer the control-plane request
    -> retry later
```

A total-free-heap guard was subsequently added. Current code requires at least
70 KiB total free heap and at least 31 KiB largest free block before starting a
Minis TLS connection.

Later optimization reduced the ESP32-C3 transport buffer and prepend capacity
from 4 KB to 2 KB while keeping the proven 8 KB directional ring buffers. That
raised measured free heap with one active forwarded channel from roughly
64-65 KB to roughly 72-73 KB.

A TLS attempt at approximately:

```text
free=72848
largest=32756
```

still failed with an mbedTLS memory-allocation error. The SSH session and active
forwarded channel remained connected and `Bytes Dropped` stayed at zero.

This confirms two things:

1. the current guard safely avoids known lower-memory cases but is not a TLS
   success guarantee;
2. further work should prefer reducing unnecessary permanent RAM over lowering
   the guard or disconnecting SSH.

## Current control-plane request

The original measurement plan referred to a separate heartbeat request. That is
no longer the current architecture.

After bootstrap, the periodic heartbeat and configuration retrieval are now one
request:

```text
GET /hb/<SID>/cfg.txt
```

The former separate `HEAD /hb/<SID>/ping` request was removed to avoid two TLS
handshakes per cycle.

## Current test rule for future memory changes

For any future memory-sensitive optimization, use the current firmware and
compare the same three operating states:

### A. SSH connected, no forwarded channel

Verify:

```text
Tunnel State: Connected
Active Channels: 0
Bytes Dropped: 0
```

Record free heap, largest free block, minimum heap, and the result of a scheduled
Minis `GET cfg.txt`.

### B. One real forwarded channel active

Generate real traffic and verify:

```text
Tunnel State: Connected
Active Channels: 1
Bytes Dropped: 0
```

Keep the forwarded connection active across a scheduled Minis request. A
deferred or failed TLS request is acceptable only if SSH traffic remains usable,
keepalives continue, and no payload is dropped.

### C. Close only the forwarded channel

Close the forwarded client without rebooting or intentionally disconnecting the
main SSH session. Confirm that channel memory is released and that a later Minis
request can succeed again when enough heap is available.

## Normal test commands

Use only:

```text
esp32_c3_lowmem
```

Do not erase flash for normal memory testing.

```powershell
pio run -e esp32_c3_lowmem
pio run -e esp32_c3_lowmem -t upload
pio device monitor -e esp32_c3_lowmem --baud 115200
```

## Pass criteria for future optimization

A memory optimization is only accepted when real runtime evidence still shows:

- the main SSH session remains stable;
- an active forwarded channel remains usable;
- SSH keepalives continue;
- `Bytes Dropped` remains `0`;
- channel close releases its memory again;
- control-plane failure or deferral does not disturb SSH;
- idle control-plane requests still succeed.

For current thresholds, rejected approaches, measured values, and design rules,
see `ESP32_C3_MEMORY_NOTES.md`.
