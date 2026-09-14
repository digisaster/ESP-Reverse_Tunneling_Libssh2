# ESP32-C3 memory and stability notes

Last validated: 2026-09-14

This document records the measured ESP32-C3 memory behaviour, rejected approaches, and design constraints for the low-memory reverse SSH tunnel build. It exists to prevent future work from repeating experiments that have already been disproven.

## Scope

Primary target:

- Environment: `esp32_c3_lowmem`
- Board class: ESP32-C3 without PSRAM
- Reverse SSH tunnel: libssh2 over Arduino-ESP32 / pioarduino
- Minis control plane: HTTPS on `cloud.supcom.nl`
- Heartbeat endpoint: `HEAD /hb/<SID>/ping`

At the time this note was created, the validated code baseline included commit:

- `4298e0774cb09ec274045d6f859fc29d95ece653` - lower Minis TLS guard to 31 KB

The current repository head may be newer. Always inspect current code before changing behaviour.

## Primary design rule

**SSH tunnel stability has priority over control-plane heartbeat delivery.**

A heartbeat may be deferred or fail temporarily. A normal heartbeat must never intentionally tear down a working SSH tunnel or active forwarded channel.

When deciding between keeping an active SSH channel alive and forcing a heartbeat through, keep SSH alive.

## Proven working low-memory profile

Use:

```text
esp32_c3_lowmem
```

Do not restore the removed experimental dynamic-mbedTLS profile unless there is new upstream evidence that changes the underlying behaviour.

The validated low-memory tunnel configuration uses approximately:

- libssh2 transport buffer: 4096 bytes
- maximum active forwarded channels: 1
- ring buffer to local side: 8192 bytes
- ring buffer to remote side: 8192 bytes
- SSH keepalive: 30 seconds

The 8 KB ring buffers are proven to support real forwarded traffic and should not be reduced merely to create space for heartbeat TLS.

## Current heartbeat model

The heartbeat is intentionally short-lived:

```text
HTTPS HEAD /hb/<SID>/ping
Connection: close
```

The TLS client is created for the request and released afterwards.

There is no intended persistent heartbeat TLS session.

There is no intended 45-second `/keepalive` TLS probe loop.

This simplified model was introduced because the earlier persistent-TLS design consumed valuable heap while not remaining reliably persistent through Cloudflare/server behaviour.

## Measured healthy idle state

With the SSH tunnel connected and no active forwarded channel, measured values after cleanup were approximately:

```text
Free heap:       89-91 KB
Largest block:   about 34.8 KB before a heartbeat
Active channels: 0
Tunnel state:    Connected
```

A heartbeat can complete successfully in this state. Example measured sequence:

```text
TLS heap heartbeat-before:    free about 90 KB, largest about 34.8 KB
TLS heap heartbeat-connected: free about 43 KB
Heartbeat HTTP status: 200
```

After the short-lived TLS client is released, free heap returns to the normal SSH idle range.

## Measured active-channel state

After opening one real forwarded SSH channel, measured values are approximately:

```text
Free heap:       61-63 KB
Largest block:   32756 bytes
Active channels: 1
Tunnel state:    Connected
```

Real forwarded traffic has been observed running for an extended period with:

- `Active Channels: 1`
- hundreds of kilobytes transferred
- `Bytes Dropped: 0`
- SSH keepalives continuing normally

This proves the channel/ring-buffer design itself is functional.

## Heartbeat behaviour while an SSH channel is active

The current Minis TLS guard is:

```cpp
constexpr size_t MINIS_TLS_MIN_LARGEST_BLOCK = 31 * 1024;
```

With an active forwarded channel, a measured state was:

```text
free=62880
largest=32756
```

This passes the 31 KB guard, but mbedTLS can still fail the handshake with:

```text
SSL - Memory allocation failed
```

This is an important result.

It means that `largest >= 31 KB` alone does **not** guarantee enough total/usable heap for a new TLS connection when an SSH channel is active.

The critical safety observation is that this failure is handled without damaging the tunnel:

- tunnel remains `Connected`
- active channel remains `1`
- existing SSH traffic continues
- SSH keepalives continue
- no bytes are dropped

This is currently acceptable behaviour.

## Behaviour after closing the forwarded channel

This behaviour has been reproduced structurally:

1. active channel consumes enough heap that a heartbeat TLS handshake may fail;
2. only the forwarded client/channel is closed;
3. the main SSH tunnel remains connected;
4. channel ring buffers and associated resources are released;
5. available heap increases again;
6. later heartbeat attempts succeed again.

Therefore a missed heartbeat while a forwarded channel is active is not currently considered a tunnel failure.

Do not solve this by aggressively reducing SSH buffers or by intentionally disconnecting SSH.

## Possible future refinement

A future cleanup may choose to skip heartbeat TLS earlier based on both:

- largest free block; and
- total free heap.

For example, measurements suggest that an active-channel state around 61-63 KB free heap is too tight for a reliable new TLS handshake even though the largest block is about 32 KB.

This should only be implemented after measuring and choosing a threshold from real data. Do not guess a threshold solely from the current observation.

The preferred behaviour would be:

```text
insufficient memory for heartbeat
    -> defer heartbeat
    -> leave SSH and active channel untouched
```

rather than intentionally invoking mbedTLS when failure is already predictable.

## Rejected approach: 48 KB TLS largest-block guard

An earlier guard required approximately 48 KB contiguous heap before allowing a Minis TLS reconnect.

That was too conservative.

Measured idle SSH state commonly had a largest block around 36.8 KB, yet real TLS handshakes completed successfully when permitted to run.

Therefore the 48 KB requirement was an application-level restriction, not an mbedTLS requirement.

Do not restore the 48 KB guard without new evidence.

## Rejected approach: persistent heartbeat TLS

An earlier design created a global `WiFiClientSecure` heartbeat client and attempted to keep the TLS connection alive with `/keepalive` requests roughly every 45 seconds.

This was removed because:

- the connection did not remain reliably persistent;
- repeated TLS reconnects still occurred;
- the persistent/global TLS state consumed valuable heap;
- with that design present, real SSH channel allocation could fail from insufficient heap.

Observed failing channel state before cleanup included roughly:

```text
free total about 29.7 KB
largest about 17.4 KB
```

while channel allocation required:

```text
need total 32768 bytes
need contiguous block 8192 bytes
```

After removing the persistent heartbeat TLS machinery, idle free heap returned to roughly 90 KB and real channel allocation worked again.

Do not reintroduce persistent heartbeat TLS unless a new implementation is proven to retain less memory and provides a clear benefit.

## Rejected approach: dynamic mbedTLS buffer rebuild

The experimental hybrid environment using:

```text
CONFIG_MBEDTLS_DYNAMIC_BUFFER=y
```

was tested and abandoned.

Even when isolating the base dynamic-buffer option and disabling the more aggressive dynamic-free options, HTTPS bootstrap reproducibly crashed during the TLS handshake with an instruction access fault.

Observed stack path included mbedTLS handshake/setup functions and `NetworkClientSecure::connect()`.

The crash occurred with ample free heap, so it was not a normal out-of-memory condition.

The experiment used the Arduino-ESP32 3.3.0 / pioarduino environment then in use.

The experimental `esp32_c3_tls_lowmem` environment and generated hybrid build artefacts were removed from the project.

Do not recreate this route merely as a memory optimization experiment. Revisit only if the framework/toolchain changes and there is specific evidence the incompatibility has been fixed.

## Rejected approach: plain HTTP heartbeat

The Minis control plane and heartbeat should remain HTTPS.

A plain HTTP heartbeat was considered during memory investigation but is not an acceptable final architecture.

Do not move authoritative/control-plane traffic to unencrypted HTTP as a memory workaround.

## Existing config-fetch recovery mechanism

The project still contains a memory-recovery path for `cfg.txt` retrieval that can request a temporary SSH pause when TLS memory pressure prevents the config fetch.

This mechanism predates the final heartbeat simplification.

It has **not yet been proven unnecessary** under all required config-fetch scenarios.

Do not remove it solely because heartbeat TLS now behaves better.

Before removing or simplifying it, test `GET /cfg.txt` under realistic SSH memory states and verify that:

- configuration can still be refreshed reliably;
- active user channels are never interrupted for normal control-plane polling;
- failure safely preserves the existing cached configuration.

## Config polling still needs a deliberate final decision

Historically documentation stated that `cfg.txt` was checked periodically after heartbeats.

Current code evolution removed some of that periodic fetching to protect SSH stability.

Before final documentation cleanup, explicitly decide and test the desired runtime behaviour for config polling.

Do not assume older README wording reflects current code.

## SSH listener/reconnect behaviour

A stale reverse listener on the SSH server can remain briefly after an ESP reboot or disconnect.

During that period:

- TCP may connect;
- authentication may succeed;
- reverse listener creation may fail;
- later retry succeeds after the stale server-side listener disappears.

The existing reconnect/backoff state machine handles this and should not be replaced with aggressive reconnect loops.

## ECDSA key parsing issue already fixed

An earlier intermittent ECDSA authentication problem was traced to PEM termination inside the pinned `libssh2_esp` dependency.

The generated PEM buffer was not reliably NUL terminated, causing intermittent parsing/signing failures depending on the following heap byte.

This was fixed previously.

Do not reopen ECDSA key-format investigation merely because an unrelated tunnel or memory failure occurs. Distinguish:

- TCP failure;
- SSH session initialization failure;
- handshake failure;
- authentication/key failure;
- reverse-listener failure;
- channel allocation failure.

## Flash/test rules

For normal firmware testing:

- do not erase flash;
- preserve WiFi configuration and local SSH private key;
- use the normal `esp32_c3_lowmem` environment;
- do not rebuild the abandoned hybrid TLS environment.

Normal commands:

```powershell
pio run -e esp32_c3_lowmem
pio run -e esp32_c3_lowmem -t upload
pio device monitor -e esp32_c3_lowmem --baud 115200
```

A full erase should only be used when there is explicit evidence that persisted flash/LittleFS state itself is the problem.

## PlatformIO/SCons workstation note

Some Windows workstations using this project encountered a pioarduino PlatformIO dependency mismatch where the active platform requested:

```text
tool-scons 4.40801.0
```

and failed with:

```text
ModuleNotFoundError: No module named 'SCons.Tool.FortranCommon'
```

The working local fix was to update the active pioarduino platform package reference to:

```text
tool-scons 4.41101.0
```

Only apply this workstation fix when that specific build error is present. It is a local build-environment issue, not an ESP firmware design requirement.

## Generated hybrid artefacts that may be deleted

The abandoned dynamic-mbedTLS experiment could leave untracked files/directories such as:

```text
.dummy/
managed_components/
sdkconfig.defaults
sdkconfig.esp32_c3_tls_lowmem
```

These are experiment artefacts and may be removed when they are not intentionally used by another local build.

Do not commit them as part of the stable firmware.

## Development rules for future optimization

When changing memory-sensitive code:

1. measure first;
2. change one mechanism at a time;
3. compare free heap, minimum heap, and largest block;
4. test idle tunnel and a real forwarded channel;
5. verify `Bytes Dropped` remains zero;
6. test SSH keepalive continuity;
7. test heartbeat before, during, and after channel activity;
8. never claim runtime success from build success alone;
9. prefer removing mechanisms over stacking additional recovery mechanisms;
10. preserve SSH first, heartbeat second.

A change is only an improvement when the real forwarded tunnel remains at least as stable as before.

## Current accepted behaviour summary

The current memory model is intentionally asymmetric:

```text
SSH tunnel idle
    -> enough memory for heartbeat TLS
    -> heartbeat normally succeeds

forwarded SSH channel active
    -> SSH channel has priority
    -> heartbeat TLS may fail or be deferred
    -> SSH remains connected and usable

forwarded channel closes
    -> channel memory is released
    -> later heartbeat can succeed again
```

This behaviour has been reproduced and is presently considered a valid baseline for further cleanup and optimization.
