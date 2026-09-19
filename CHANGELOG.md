# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased] — 2026-09-17

This is the candidate baseline for `esp32tun` reference firmware
`1.0.0-beta.1`. The Arduino library retains its existing 2.x version line.

### Added

- Added a bounded ESP32-to-Minis alert queue. Runtime alerts are posted to the
  existing Minis alert endpoint only by the background control-plane task, so
  alert producers never open their own TLS session and the existing TLS memory
  guard remains authoritative.
- Successful first Genesis upload now emits an informational Minis alert.
  Managed cfg.txt changes persist a one-shot notice before restart; the alert is
  sent after reboot and its marker is cleared only after successful delivery.
- Added a minimal, non-blocking status LED for the ESP32-C3 reference profile.
  It distinguishes missing configuration, active setup, connection attempts,
  an established tunnel, and connection or authentication errors.
- First-boot WiFi provisioning for the reference firmware. A temporary setup
  access point scans for networks, verifies the supplied credentials, writes
  `/esp32tun.cfg`, and restarts without activating the setup server again.
- The WiFi password field now has a visibility toggle in the captive portal.
- WiFi MAC-vendor profiles were added to the reference firmware. The operator
  can keep the original ESP32 MAC prefix or select Cisco (`00:00:0C`) or HP
  (`3C:D9:2B`). Only the first three octets are replaced; the final three remain
  device-specific.
- `MAC_VENDOR=ORIGINAL|CISCO|HP` is accepted as an optional Minis-managed
  setting. When omitted, the locally stored MAC profile is preserved.
- New Genesis reports include the factory hardware MAC, active WiFi MAC, and
  selected MAC-vendor profile. Genesis remains one-time per SSH key identity.
- Two-phase provisioning now configures password or private-key SSH
  authentication and one reverse tunnel without editing source files.
- The tunnel setup page can store a matching OpenSSH public-key line. This is
  required for ECDSA authentication and remains optional for RSA-PEM keys.
- Dedicated native regression coverage for configured channel inactivity,
  disabled timeouts, and `millis()` wraparound.
- Initial `esp32_c3_lowmem` build profile for a single reverse listener and
  active channel on ESP32-C3 boards without PSRAM.
- Minis `cfg.txt` can activate, disable, and replace the reference firmware's
  single tunnel. The lowercase SID is used as SSH username while the existing
  device-local key pair is retained.
- A total-free-heap guard was added to the Minis HTTPS path. On ESP32-C3 the
  control plane requires at least 70 KiB total free heap and a largest free
  block of at least 31 KiB before attempting TLS.
- One-time ESP32 Genesis inventory upload matching the Minis upload model.

### Changed

- Removed pre-release compatibility/dead-code paths that were not used by the
  current firmware or tests: file-path SSH key authentication, legacy
  configuration macros/placeholders, the inactive listener-relisten watchdog,
  the hard-coded multi-tunnel firmware demo, and the obsolete managed-setup
  close helper. The generated `examples/sdkconfig.esp32dev` file was also
  removed; supported builds remain defined by PlatformIO environments.

- BOOT-button behaviour is now explicit and non-destructive by default:
  3 short clicks reset WiFi only, holding BOOT for 4 seconds reopens the stored
  tunnel configuration, and 5 short clicks perform a full factory reset.
- Managed Minis configuration follows a persist-and-restart model. A valid
  changed `cfg.txt` is written to `/esp32tun.cfg` and the device restarts; the
  new tunnel and MAC-vendor settings are applied through the normal boot path.
- MAC-vendor changes are applied before WiFi initialization and are never
  switched live while the WiFi interface is active.
- Device configuration format is now version 4 and stores `mac_vendor`.
  Versions 1 through 3 remain readable and default to `ORIGINAL`.
- Periodic Minis heartbeat and config retrieval are one request:
  `GET /hb/<sid>/cfg.txt`. The separate `HEAD /ping` transaction was removed.
- The ESP32-C3 low-memory transport buffer was reduced from 4 KB to a shared
  2 KB work buffer and the per-ring prepend capacity to 2 KB. The proven 8 KB
  directional channel rings remain unchanged.
- The Minis cache stores only `HB_INTERVAL_MIN`; tunnel and MAC-vendor settings
  remain in the device runtime configuration file.

### Fixed

- Fixed three defects in the pinned `libssh2_esp` RSA path: a one-byte public
  key buffer overflow, a missing RSA-context initialization, and an
  uninitialized public-key derivation result.
- Configure SSH keepalive only after successful authentication.
- Changed libssh2 keepalive to `want_reply=0`, removing the measured reply-path
  heap leak while retaining periodic keepalive traffic.
- PlatformIO consumer projects now run the pinned dependency's RSA
  compatibility patch through `library.json`.
- The root PlatformIO project no longer compiles `examples/src/main.cpp`
  twice.
- `setBufferConfig(..., channelTimeout, ...)` now stores and applies the
  configured timeout in both transport and channel-slot recycling.
- The ring-buffer prepend capacity can be reduced at build time using
  `SSH_TUNNEL_PREPEND_CAPACITY`; the default remains 8 KB for compatibility.
- Tunnel byte counters remain cumulative when channel slots close or are reused,
  and `Bytes Dropped` reports actual discarded buffered payload.
- Repeated partial writes preserve FIFO order when prepend data is already
  pending.
- Heap warnings account for the requested allocation and a small operating
  reserve instead of treating every healthy sub-50-KB ESP32-C3 heap as low.
- Public-key failures report actionable key-pair and `authorized_keys` checks.
- Optional LittleFS cleanup checks for temporary files before removing them.

### Validated

- WEMOS LOLIN S2 Mini release build and tunnel operation remain validated from
  the beta baseline.
- Interactive SSH forwarding remained connected for 45 minutes without the
  former 30-second idle disconnect.
- ESP32-C3 hardware testing confirmed an active forwarded SSH channel,
  repeated channel close and reopen, 30-second keepalive messages, zero dropped
  bytes, and heap recovery after channel closure.
- ESP32-C3 hardware testing confirmed unencrypted RSA-PEM private-key
  authentication and reverse-listener creation.
- ESP32-C3 hardware testing confirmed ECDSA P-256 authentication using an
  EC-PEM private key and matching OpenSSH public-key line.
- Minis-managed settings survived hardware reset, authenticated directly as
  the lowercase SID, recreated the reverse listener, and forwarded an
  interactive channel with zero dropped bytes.
- The 2 KB shared transport/prepend profile reached roughly 2.5 Mbit/s in a
  10 MB transfer test with zero dropped bytes.
- Heartbeat/config HTTPS requests and an idle SSH tunnel operate concurrently;
  under active-channel memory pressure the TLS guard defers the control-plane
  request and leaves SSH untouched.
- Genesis upload was verified once per SSH key tag; a normal reboot reported
  `Genesis already recorded` and did not create a duplicate file.

### Documentation

- Updated the root README, example guide, and technical documentation index to
  the current BOOT-button behaviour, shared 2 KB transport buffer, one-request
  heartbeat/config model, Genesis flow, and MAC-vendor support.
- Added `docs/WIFI_MAC_VENDOR.md` covering local and Minis configuration,
  reboot semantics, SID stability, Genesis fields, and operational caveats.
- Removed stale documentation claims about the older BOOT/reset mapping and
  separate periodic ping requests.

## [2.2.0] — 2026-04-30 — Stabilization

### Fixes

- **Bug #1 — channel re-allocation race causing small-chunk byte loss.**
  Two-part fix:
  - 50 ms cooldown after `finalizeClose` before a slot can be re-bound,
    so libssh2's channel free has time to settle before the slot is
    reused (`082e8d4`).
  - Forward the SSH EOF to the local TCP socket via `shutdown(SHUT_WR)`
    as soon as `remoteEof` is set and the outbound ring is drained.
    Echo-style backends only respond to bytes we send them; without
    this, they never volunteer EOF on their own and the channel stayed
    in half-close until `HALF_CLOSE_TIMEOUT_MS` (5 s) fired, blocking
    back-to-back tunnel reuse (`4e174d2`).

  Repro count via `test_echo_repeat` (1 KB and 256 B chunks):
  - Pre-fix: 15/15 cycles fail
  - After cooldown only: 4/15 cycles fail
  - After EOF forwarding: 0/15 in 10+ consecutive runs

- **Bug #2 — stale `tcpip-forward` listeners survived ESP32 reconnects.**
  When the ESP32's TCP link died, libssh2 could not deliver
  `cancel-tcpip-forward` to sshd, which kept the listener bound. The
  next reconnect's forward request was rejected with
  `bind [::]:22080: Address in use`, leaving the tunnel in a
  "Connected but zero-forward" state. Logged at warning level now and
  documented as requiring sshd-side `ClientAliveInterval` to fully
  reap zombie sessions (`3449d82`).

### Additions

- `SSHSession::getActiveListenerCount()` (and matching forwarder on
  `SSHTunnel`) for diagnostics.
- `STATS_TEST` integration-test telemetry now reports
  `listeners_ready=N` so harness fixtures can wait deterministically
  for all configured forwards to be bound (test build only).

### Diagnostics & docs

- **Bug #3 — channel teardown latency on high-RTT links** is resolved
  as a side-effect of Bug #1's EOF-forwarding fix: with the local
  socket now being shut down on remote EOF, the echo-side EOF arrives
  promptly and the existing `(remoteEof && localEof)` branch closes
  the channel without burning the 5 s grace window. Test D (50 cycles
  on LAN) returns to `ch=0` well within the 3 s threshold for every
  cycle. No dedicated fast-path needed.
- New integration test suites under `test/integration/harness/`:
  Tests B (throughput stability), D (channel leak), F (reconnect),
  G (circuit breaker + slow consumer), and a Bug #1 narrowing
  variant of Test A (`test_echo_repeat_small`/`_mid`).
- New host-only Unity tests for `circuit_breaker`, `ssh_config`
  validators, and `prepend_buffer` (33/33 PASS in CI).
- Test reports under `docs/superpowers/test-reports/` (untracked,
  local-only) record the validation runs.

### Deployment notes

- **Recommended sshd config** for the remote bastion:
  `ClientAliveInterval 15` and `ClientAliveCountMax 2`. The library
  cannot release listeners on a network-killed session by itself —
  sshd needs to reap zombies for the next reconnect's
  `tcpip-forward` to bind cleanly. Without this, Bug #2 can still
  manifest after a hard network drop even with this release's
  firmware fixes.
