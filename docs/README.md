# Documentation

This directory contains the current technical guides for the library and the
`esp32tun` reference firmware.

Start with the repository root `README.md` for normal build, provisioning, and
runtime use. Use the documents below only when working on the corresponding
subsystem.

## Current technical guides

### [ESP32_C3_MEMORY_NOTES.md](ESP32_C3_MEMORY_NOTES.md)
Anti-regression rules for the ESP32-C3 low-memory profile:

- SSH has priority over control-plane TLS;
- shared 2 KB transport buffer and proven 8 KB directional rings;
- Minis TLS heap guard;
- one `GET /hb/<SID>/cfg.txt` heartbeat/config request;
- keepalive `want_reply=0`;
- real-hardware acceptance criteria for memory-sensitive changes.

### [WIFI_MAC_VENDOR.md](WIFI_MAC_VENDOR.md)
Reference-firmware WiFi MAC profiles:

- `ORIGINAL`, `CISCO`, and `HP`;
- captive-portal and `MAC_VENDOR` configuration;
- reboot-only application;
- SID/SSH identity stability;
- Genesis MAC reporting.

### [SSH_KEYS_MEMORY.md](SSH_KEYS_MEMORY.md)
Current SSH-key behaviour:

- automatic device-local ECDSA P-256 identity generation;
- LittleFS key storage;
- manual key provisioning;
- validated and unsupported key formats.

### [RSA_KEY_AUTH_FIX.md](RSA_KEY_AUTH_FIX.md)
Why the pinned `libssh2_esp` RSA compatibility patch must remain in the build.
This document is intentionally retained because removing or weakening that
patch can reintroduce a known authentication regression.

### [HOST_KEY_VERIFICATION.md](HOST_KEY_VERIFICATION.md)
Current host-key verification API and reference-firmware status.

## Other useful project documentation

- [`../examples/README.md`](../examples/README.md): reference-firmware runtime
  and managed configuration details.
- [`../test/README`](../test/README): test layout.
- [`../test/integration/README.md`](../test/integration/README.md): hardware
  integration-test harness.
- [`../CHANGELOG.md`](../CHANGELOG.md): historical project changes.

## Documentation rule

Normal guides describe the **current implementation**. Historical experiments
belong in the changelog unless their result is needed to prevent a likely
future regression. This keeps a new reader from having to reconstruct the
project's development history before understanding how it works today.

**Last review:** 2026-09-17
