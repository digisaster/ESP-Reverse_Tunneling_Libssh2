# ESP32-C3 stability and memory constraints

Last reviewed: 2026-09-17

This document contains only the ESP32-C3 design constraints that should survive
future refactoring because they are backed by real hardware tests or prevent a
known regression.

## Priority rule

**SSH tunnel stability has priority over Minis/control-plane delivery.**

A heartbeat/config request may be deferred or fail temporarily. It must not tear
down a healthy SSH session or forwarded channel merely to create memory for
TLS.

## Proven low-memory profile

The `esp32_c3_lowmem` reference target uses:

```text
transport work buffer:  2048 bytes, shared by RX/TX phases
max active channels:    1
ring buffer per side:   8192 bytes
prepend capacity:       2048 bytes per ring
SSH keepalive:          30 seconds
```

The 8 KB directional rings have been validated under real forwarded traffic
with zero dropped bytes. Do not shrink them simply to make more room for TLS.

The shared 2 KB transport buffer is a good production compromise. A measured
10 MB transfer reached roughly 2.5 Mbit/s with zero dropped bytes. Do not resume
datapump optimization unless actual deployment requirements show that this is
insufficient.

## Minis heartbeat/config model

After bootstrap, heartbeat and managed configuration use one request:

```text
GET /hb/<SID>/cfg.txt
Connection: close
```

There is no separate periodic `HEAD /ping` request and no persistent heartbeat
TLS connection.

Runtime ESP32-to-Minis alerts must not create a second TLS worker. Alert
producers enqueue bounded messages only; the existing Minis background task
owns delivery to `/uploot.php` and applies the same TLS memory guard as the
heartbeat/config request. Failed/deferred delivery leaves the message queued
for a later control-plane opportunity.

A changed valid managed configuration is persisted and applied after
`ESP.restart()`. There is no live tunnel replacement or control-plane
pause/resume state machine.

## TLS memory guard

Before starting Minis TLS the current C3 firmware requires both:

```cpp
MINIS_TLS_MIN_FREE_HEAP      = 70 * 1024
MINIS_TLS_MIN_LARGEST_BLOCK = 31 * 1024
```

If either test fails, the request is deferred and SSH is left untouched.

These checks exist because a largest-free-block test alone was proven
insufficient. TLS has also failed just above the thresholds while SSH remained
healthy, so the guard is a safety boundary, not a guarantee of handshake
success.

Do not lower the guard merely to force a heartbeat through. A missed
control-plane cycle during heavy forwarded traffic is accepted behaviour.

## Keepalive rule

libssh2 keepalive is configured **after authentication** and uses
`want_reply=0`.

A reply-requesting keepalive (`want_reply=1`) produced a measurable recurring
heap leak. Do not change this back without repeatable hardware evidence that the
underlying behaviour has changed.

## Approaches not to reintroduce casually

The following designs were tested or removed because they added memory pressure
or complexity without improving the primary requirement:

- a separate periodic `HEAD /ping` plus a second `GET cfg.txt` TLS handshake;
- persistent/global `WiFiClientSecure` heartbeat state;
- pausing or reconnecting SSH to make room for control-plane TLS;
- a largest-block-only TLS guard;
- reducing the proven 8 KB directional channel rings for control-plane memory;
- dynamic-mbedTLS-buffer experiments on the tested Arduino-ESP32/pioarduino
  toolchain, which were unstable during TLS setup.

The dynamic-mbedTLS point may be revisited only after a framework/toolchain
change and with new runtime evidence.

## Stale reverse listener after reconnect

After a hard ESP reset or network loss, sshd can temporarily retain the old
`tcpip-forward` listener. During that window authentication can succeed while
listener creation fails. The normal retry path generally succeeds after sshd
releases the stale session.

This is server-side session cleanup behaviour, not evidence by itself of an
ESP32 SSH handshake defect. Investigate only if it becomes persistent or needs
multiple/minutes-long retries.

## Test rules for memory-sensitive changes

For any change to buffers, TLS, keepalive, reconnect, or channel handling,
validate on real hardware in all three states:

1. SSH connected with no forwarded channel;
2. one real forwarded channel carrying traffic;
3. the forwarded channel closed again while the outer SSH session remains up.

A change is accepted only when:

- the SSH session remains stable;
- forwarded traffic remains usable;
- `Bytes Dropped` remains `0`;
- keepalives continue;
- channel memory is released after close;
- a failed/deferred Minis request leaves SSH untouched;
- Minis requests work again when sufficient memory is available.

Build success alone is not runtime validation.

## Flash rule

Do not erase flash for normal firmware tests. Preserve WiFi configuration and
the device-local SSH identity unless persisted state itself is the subject of
the test.

A full erase/factory reset should be deliberate because it changes the SSH
identity and therefore also the Genesis identity marker.
