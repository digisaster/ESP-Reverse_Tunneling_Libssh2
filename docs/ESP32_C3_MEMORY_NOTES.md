# ESP32-C3 memory and stability notes

Last validated: 2026-09-15

This document is the authoritative record of the measured ESP32-C3 memory
behaviour, rejected approaches, and design constraints for the low-memory
reverse SSH tunnel build. It exists to prevent future work from repeating
experiments that have already been disproven.

## Scope

Primary target:

- Environment: `esp32_c3_lowmem`
- Board class: ESP32-C3 without PSRAM
- Reverse SSH tunnel: libssh2 over Arduino-ESP32 / pioarduino
- Minis control plane: HTTPS on `cloud.supcom.nl`
- Periodic control-plane request: `GET /hb/<SID>/cfg.txt`

The current runtime design was simplified through commit
`543a34666ff90701dabfefcf570e1a8cd482e799` (`Replace ping heartbeat with cfg
fetch`). Always inspect current code before changing behaviour because the
repository head may be newer than this note.

## Primary design rule

**SSH tunnel stability has priority over control-plane delivery.**

A heartbeat/config request may be deferred or fail temporarily. A normal
control-plane request must never intentionally tear down a working SSH session
or active forwarded channel.

When deciding between keeping an active SSH channel alive and forcing a Minis
request through, keep SSH alive.

## Proven working low-memory profile

Use:

```text
esp32_c3_lowmem
```

The current low-memory tunnel configuration is:

- libssh2 transport buffer: 2048 bytes
- maximum active forwarded channels: 1
- ring buffer to local side: 8192 bytes
- ring buffer to remote side: 8192 bytes
- prepend capacity per ring: 2048 bytes
- SSH keepalive: 30 seconds

The 8 KB directional ring buffers are proven with real forwarded traffic and
should not be reduced merely to create room for Minis TLS.

The 2 KB transport/prepend profile replaced the earlier 4 KB values and gained
approximately 8 KB of free heap while preserving the 8 KB channel rings.

## Current Minis heartbeat/config model

After bootstrap, the periodic heartbeat and configuration check are the same
request:

```text
HTTPS GET /hb/<SID>/cfg.txt
Connection: close
```

There is no separate periodic `HEAD /ping` request. One scheduled cycle means
one short-lived TLS connection and one `GET cfg.txt`.

The task also performs an initial `cfg.txt` fetch shortly after startup.

There is no intended persistent heartbeat TLS session and no 45-second
`/keepalive` TLS probe loop.

When a complete valid `cfg.txt` differs from the stored tunnel settings, the
firmware writes the managed configuration and restarts. The new tunnel settings
are applied cleanly on the next boot. There is no live tunnel replacement,
rollback state machine, or control-plane SSH pause/resume mechanism.

Invalid/incomplete config, an unavailable server, or a failed/deferred TLS
request leaves the current stored configuration and SSH session untouched.

## TLS memory guard

Before creating a Minis TLS connection the C3 currently requires both:

```cpp
constexpr size_t MINIS_TLS_MIN_FREE_HEAP = 70 * 1024;
constexpr size_t MINIS_TLS_MIN_LARGEST_BLOCK = 31 * 1024;
```

If either condition is not met, the request is deferred and SSH is left
untouched.

The 70 KiB total-free guard was introduced after measurements showed that a
31 KiB largest-block test by itself was insufficient. It successfully prevents
known low-memory attempts around the old 61-65 KB active-channel range.

The guard is a safety threshold, not a guarantee that TLS will succeed. Later
measurements with the 2 KB transport/prepend profile showed that TLS can still
fail just above the threshold.

## Latest measured healthy idle state

With the SSH session connected and no active forwarded channel, the 2 KB
profile measured approximately:

```text
Free heap:       96-97 KB
Largest block:   about 38.9 KB
Active channels: 0
Tunnel state:    Connected
```

An observed initial `cfg.txt` TLS sequence was:

```text
TLS heap before:    free=97564 largest=38900
TLS heap connected: free=48404 largest=32756
```

The short-lived TLS client is then released and heap returns to the normal SSH
idle range.

## Latest measured active-channel state

With one real forwarded SSH channel active after the 2 KB transport/prepend
change, measured values were approximately:

```text
Free heap:       72.5-73.4 KB
Largest block:   32756 bytes
Active channels: 1
Tunnel state:    Connected
Bytes Dropped:   0
```

This is roughly 8 KB better than the previous 4 KB transport/prepend profile,
which commonly sat near 64-65 KB with one active channel.

The active channel remained stable and SSH keepalives continued normally.

## Important TLS result at about 72.8 KB free

A scheduled Minis request was attempted while the forwarded channel was still
active with:

```text
free=72848
largest=32756
```

The current 70 KiB / 31 KiB guard allowed the attempt, but mbedTLS still failed
with:

```text
SSL - Memory allocation failed
```

After the failure:

- tunnel remained `Connected`;
- active channel remained `1`;
- `Bytes Dropped` remained `0`;
- SSH keepalives continued;
- the next scheduled control-plane cycle could succeed after the channel was
  closed and its buffers were released.

This result proves that approximately 72.8 KB total free heap is still not a
reliable success point for a new TLS handshake. Do not lower the guard in an
attempt to force heartbeats through.

It does **not** yet justify raising the guard as a substitute for further memory
cleanup. The preferred direction remains to reduce unnecessary permanent RAM
while keeping the proven SSH buffers intact.

## Behaviour after closing the forwarded channel

The following behaviour is validated:

1. an active channel consumes enough heap that Minis TLS may fail or be
   deferred;
2. only the forwarded client/channel is closed;
3. the main SSH session remains connected;
4. channel rings and related resources are released;
5. available heap returns to the idle range;
6. a later `GET cfg.txt` can succeed again.

A missed heartbeat/config cycle while a forwarded channel is active is not a
tunnel failure.

## Managed configuration application

The current model deliberately avoids complex live reconfiguration:

```text
scheduled cfg.txt GET
    -> request fails/deferred: keep everything as-is
    -> invalid/incomplete cfg: ignore
    -> same tunnel settings: no-op
    -> changed tunnel settings: persist /esp32tun.cfg and ESP.restart()
```

The SSH private/public key files remain separate in LittleFS and are not
rewritten by the managed tunnel-config save.

On reboot the normal provisioning loader restores the stored key material and
starts the tunnel using the newly persisted managed settings.

## Rejected approach: separate periodic HEAD heartbeat

An earlier design performed:

```text
HEAD /hb/<SID>/ping
```

and, after success, opened a second TLS connection for:

```text
GET /hb/<SID>/cfg.txt
```

Because both connections used `Connection: close`, that meant two independent
TLS handshakes per cycle. The separate HEAD request was removed. `GET cfg.txt`
now serves as both heartbeat and configuration retrieval.

Do not restore a separate periodic ping unless it provides a clear requirement
that cannot be satisfied by the existing config request.

## Rejected approach: 48 KB TLS largest-block guard

An earlier guard required approximately 48 KB contiguous heap before allowing a
Minis TLS connection.

That was too conservative. Idle SSH states with much smaller largest blocks
were able to complete real TLS handshakes. Do not restore the 48 KB requirement
without new evidence.

## Rejected approach: largest-block-only guard

A 31 KB largest-block test alone was proven insufficient. Earlier active-channel
measurements around:

```text
free=61-63 KB
largest=32756
```

could pass the largest-block test but fail TLS allocation.

This is why current code checks both total free heap and largest free block.

## Rejected approach: persistent heartbeat TLS

An earlier design created a global `WiFiClientSecure` heartbeat client and
attempted to keep TLS alive with periodic `/keepalive` requests.

It was removed because:

- the connection did not remain reliably persistent;
- repeated reconnects still occurred;
- persistent/global TLS state consumed valuable heap;
- real SSH channel allocation could fail from insufficient heap.

Do not reintroduce persistent heartbeat TLS unless a new implementation is
proven to retain less memory and provides a clear benefit.

## Rejected approach: dynamic mbedTLS buffer rebuild

The experimental profile using:

```text
CONFIG_MBEDTLS_DYNAMIC_BUFFER=y
```

was tested and abandoned. HTTPS bootstrap reproducibly crashed during the TLS
handshake with the Arduino-ESP32 3.3.0 / pioarduino environment used for the
experiment, even when ample free heap was available.

The experimental environment and generated hybrid build artefacts were removed.
Do not recreate it merely as a memory optimization experiment. Revisit only if
the framework/toolchain changes and there is specific evidence that the
incompatibility has been fixed.

## Rejected approach: plain HTTP control plane

The Minis control plane must remain HTTPS. Do not move authoritative config or
heartbeat traffic to unencrypted HTTP as a memory workaround.

## Removed approach: control-plane SSH pause/recovery

Older code contained a config-fetch recovery mechanism that could request a
temporary SSH pause in order to create memory for TLS. That mechanism has been
removed.

Current behaviour is intentionally simpler:

```text
not enough memory for Minis TLS
    -> defer the request
    -> leave SSH untouched
    -> retry on a later scheduled cycle
```

Do not reintroduce a pause/resume/reconnect state machine unless a new hard
requirement makes it necessary.

## SSH listener/reconnect behaviour

A stale reverse listener on the SSH server can remain briefly after an ESP
reboot or disconnect.

During that period:

- TCP may connect;
- authentication may succeed;
- reverse listener creation may fail;
- a later retry succeeds after the stale server-side listener disappears.

The existing reconnect/backoff state machine handles this. It should not be
replaced with aggressive reconnect loops.

## ECDSA key parsing issue already fixed

An earlier intermittent ECDSA authentication problem was traced to PEM
termination inside the pinned `libssh2_esp` dependency. The generated PEM
buffer was not reliably NUL terminated, causing intermittent parsing/signing
failures depending on the following heap byte.

That issue is fixed. Do not reopen ECDSA key-format investigation merely because
an unrelated tunnel or memory failure occurs.

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

A full erase should only be used when there is explicit evidence that persisted
flash/LittleFS state itself is the problem.

## PlatformIO/SCons workstation note

Some Windows workstations encountered a pioarduino PlatformIO dependency
mismatch where the active platform requested:

```text
tool-scons 4.40801.0
```

and failed with:

```text
ModuleNotFoundError: No module named 'SCons.Tool.FortranCommon'
```

The working local fix was to update the active pioarduino platform package
reference to:

```text
tool-scons 4.41101.0
```

Only apply this workstation fix when that specific build error is present. It
is a local build-environment issue, not an ESP firmware design requirement.

## Generated hybrid artefacts that may be deleted

The abandoned dynamic-mbedTLS experiment could leave untracked files/directories
such as:

```text
.dummy/
managed_components/
sdkconfig.defaults
sdkconfig.esp32_c3_tls_lowmem
```

These are experiment artefacts and may be removed when they are not
intentionally used by another local build. Do not commit them as part of the
stable firmware.

## Development rules for future optimization

When changing memory-sensitive code:

1. measure first;
2. change one mechanism at a time;
3. compare free heap, minimum heap, and largest block;
4. test idle tunnel and a real forwarded channel;
5. verify `Bytes Dropped` remains zero;
6. test SSH keepalive continuity;
7. test Minis config/heartbeat before, during, and after channel activity;
8. never claim runtime success from build success alone;
9. prefer removing mechanisms over stacking additional recovery mechanisms;
10. preserve SSH first, control plane second.

A change is only an improvement when the real forwarded tunnel remains at least
as stable as before.

## Current accepted behaviour summary

```text
SSH tunnel idle
    -> enough memory for Minis TLS
    -> GET cfg.txt normally succeeds

forwarded SSH channel active
    -> SSH channel has priority
    -> cfg/heartbeat TLS may fail or be deferred
    -> SSH remains connected and usable

forwarded channel closes
    -> channel memory is released
    -> a later cfg/heartbeat request can succeed again
```

This is the current baseline for further cleanup and optimization.
