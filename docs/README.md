# ESP-Reverse_Tunneling_Libssh2 Documentation

This documentation covers the ESP-Reverse_Tunneling_Libssh2 library and the
reference `esp32tun` firmware.

## Main guides

### [ESP32_C3_MEMORY_NOTES.md](ESP32_C3_MEMORY_NOTES.md)
Authoritative ESP32-C3 low-memory design and measurement record:

- current 2 KB shared transport work buffer
- 8 KB directional channel rings
- Minis TLS memory guard
- combined heartbeat/config request
- measured active-channel heap behaviour
- rejected memory approaches

### [WIFI_MAC_VENDOR.md](WIFI_MAC_VENDOR.md)
WiFi MAC-vendor profiles in the reference firmware:

- `ORIGINAL`, `CISCO`, and `HP` profiles
- captive-portal selection and persistent config
- optional Minis `MAC_VENDOR` setting
- reboot-only application model
- SID/SSH identity stability
- Genesis hardware/active MAC reporting

### [SSH_KEYS_MEMORY.md](SSH_KEYS_MEMORY.md)
SSH key authentication with in-memory use:

- SSH key configuration
- LittleFS storage
- tested key formats
- practical examples

### [HOST_KEY_VERIFICATION.md](HOST_KEY_VERIFICATION.md)
Security guide for host key verification:

- protection against man-in-the-middle attacks
- server fingerprint configuration
- verification API
- migration and troubleshooting

## Configuration

### Password authentication

```cpp
globalSSHConfig.setSSHServer("server.com", 22, "username", "password");
```

### SSH key authentication

See [RSA key authentication fix and ESP32-C3 validation](RSA_KEY_AUTH_FIX.md).

```cpp
globalSSHConfig.setSSHKeyAuthFromMemory(
    "server.com", 22, "username",
    privateKeyData, publicKeyData, ""
);
```

### Secure full configuration

```cpp
// SSH authentication
globalSSHConfig.setSSHKeyAuthFromMemory(
    "server.com", 22, "username",
    privateKeyData, publicKeyData, ""
);

// Server identity verification
globalSSHConfig.setHostKeyVerification(
    "SHA256:server_fingerprint",
    "ssh-ed25519",
    true
);

// Tunnel configuration
globalSSHConfig.setTunnelConfig(
    "127.0.0.1", 8080,
    "192.168.1.100", 80
);
```

## Supported key formats

| Format | Compatibility | Recommendation |
|--------|---------------|----------------|
| PEM RSA (`-----BEGIN RSA PRIVATE KEY-----`) | Hardware validated | Tested baseline |
| PEM EC (`-----BEGIN EC PRIVATE KEY-----`) | P-256 hardware validated | Matching public-key line required |
| PKCS#8 (`-----BEGIN PRIVATE KEY-----`) | Not systematically validated | Test before deployment |
| Modern OpenSSH (`-----BEGIN OPENSSH PRIVATE KEY-----`) | Not systematically validated | Prefer a tested PEM format |

## Supported key algorithms

| Algorithm | Support | Recommendation |
|-----------|---------|----------------|
| Ed25519 | Not compiled in mbedTLS backend | Do not use for this firmware |
| RSA | Hardware validated | 2048 bits tested |
| ECDSA P-256 | Hardware validated | Compact tested option |
| ECDSA P-384 | Not hardware-tested | Test before use |
| ECDSA P-521 | Not hardware-tested | Test before use |
| DSA | Not supported | Do not use |

## Security levels

### Development

```cpp
globalSSHConfig.setSSHServer("server.com", 22, "user", "password");
```

### Key-based authentication

```cpp
globalSSHConfig.setSSHKeyAuthFromMemory(/* SSH keys */);
```

### Production hardening

```cpp
globalSSHConfig.setSSHKeyAuthFromMemory(/* SSH keys */);
globalSSHConfig.setHostKeyVerification(/* server fingerprint */);
```

The current reference firmware still uses `setInsecure()` for Minis HTTPS and
has SSH host-key verification disabled. Both remain production-hardening tasks.

## Quick start

### Installation

```ini
lib_deps =
    https://github.com/digisaster/ESP-Reverse_Tunneling_Libssh2.git
```

### Minimal code

```cpp
#include "ESP-Reverse_Tunneling_Libssh2.h"

SSHTunnel tunnel;

void setup() {
    WiFi.begin("SSID", "PASSWORD");
    globalSSHConfig.setSSHKeyAuthFromMemory(/* parameters */);
    tunnel.init();
    tunnel.connectSSH();
}

void loop() {
    tunnel.loop();
}
```

## ESP32-C3 low-memory reference profile

The current `esp32_c3_lowmem` profile intentionally supports one active
forwarded channel and uses:

```text
transport work buffer:  2048 bytes shared by RX/TX phases
max active channels:    1
ring buffer per side:   8192 bytes
prepend capacity:       2048 bytes per ring
SSH keepalive:          30 seconds
```

Do not reduce the proven 8 KB channel rings merely to create room for Minis
TLS. See [ESP32_C3_MEMORY_NOTES.md](ESP32_C3_MEMORY_NOTES.md) before changing
memory-sensitive code.

## Reference-firmware WiFi MAC profiles

The provisioning UI can keep the factory ESP32 WiFi MAC or apply a Cisco/HP
vendor prefix while retaining the final three device-specific octets. The
selection is persisted in `/esp32tun.cfg` and is applied before WiFi starts.
Vendor changes are never made live on an active WiFi interface.

The Minis SID remains based on the ESP32 eFuse identity and therefore does not
change with the WiFi MAC profile. See
[WIFI_MAC_VENDOR.md](WIFI_MAC_VENDOR.md) for details and operational caveats.

## Minis-managed configuration

After the initial SID bootstrap, the periodic heartbeat and configuration check
are one short-lived HTTPS request:

```text
GET /hb/<sid>/cfg.txt
```

There is no separate periodic `HEAD /ping`. A valid changed configuration is
stored and the ESP32 restarts; the new settings are applied on the next boot.
Failed/deferred control-plane requests leave SSH untouched.

The optional managed field:

```ini
MAC_VENDOR=CISCO
```

accepts `ORIGINAL`, `CISCO`, or `HP`. Omitting it preserves the locally stored
selection. A changed value uses the same persist-and-restart flow as tunnel
changes.

## Genesis inventory

The reference firmware uploads one Genesis inventory report per SSH key
identity. New reports include both the factory and active WiFi addresses plus
the selected vendor profile. Normal reboot, WiFi-only reset, or MAC-vendor
change does not create a duplicate Genesis because the marker is tied to the
SSH public-key diagnostic tag.

## BOOT-button reference

Current reference-firmware behaviour:

- 3 short clicks: WiFi-only reset; SSH/tunnel/key/Minis settings and MAC vendor
  are preserved.
- hold 4 seconds: reopen stored tunnel configuration.
- 5 short clicks: full factory reset, including stored SSH keys and device
  configuration.

## Performance optimization rules

For the C3 profile:

- limit `maxChannels` to the number actually required;
- keep the tested 8 KB directional rings unless new runtime evidence supports a
  change;
- measure free heap, minimum heap, and largest free block on hardware;
- change one memory mechanism at a time;
- validate real forwarded traffic and keep `Bytes Dropped` at zero;
- prefer removing unnecessary state/tasks/buffers over adding recovery state
  machines.

For general library builds, tune transport and ring sizes to the target and
traffic pattern rather than copying the C3 values blindly.

## Recommended general configuration

```cpp
globalSSHConfig.setConnectionConfig(
    30,    // Keep-alive: 30s
    5000,  // Reconnect delay: 5s
    10,    // Max reconnect attempts
    30     // Connection timeout: 30s
);

globalSSHConfig.setBufferConfig(
    8192,       // Transport buffer size
    5,          // Max channels
    1800000,    // Channel inactivity timeout: 30 minutes
    64 * 1024   // Ring buffer size per channel, per direction
);
```

The third `setBufferConfig` argument applies to each forwarded channel, not to
the outer SSH session. Activity in either direction resets the timer. A value
of `0` keeps idle forwarded channels open indefinitely.

## Multi-tunnel / multiple listeners

Use `addTunnelMapping()` and `setMaxReverseListeners()` to expose several local
services through a single SSH connection:

```cpp
globalSSHConfig.clearTunnelMappings();
globalSSHConfig.setMaxReverseListeners(3);

globalSSHConfig.addTunnelMapping("127.0.0.1", 22080, "192.168.1.100", 80);
globalSSHConfig.addTunnelMapping("127.0.0.1", 22081, "192.168.1.150", 502);
globalSSHConfig.addTunnelMapping("127.0.0.1", 22082, "192.168.1.200", 22);
```

Listeners are created at `connectSSH()` time. Adding mappings while a session is
active requires a reconnect to take effect.

## Troubleshooting

### Authentication failed

- use a hardware-validated key format where possible;
- verify the matching public key is present in `authorized_keys`;
- test the same credentials with a standard SSH client;
- for ECDSA P-256, provide both the EC-PEM private key and matching OpenSSH
  public-key line.

### Host key verification failed

- obtain the real server fingerprint;
- verify the configured algorithm and fingerprint;
- do not bypass a mismatch in production.

### Connection timeout

- verify network connectivity;
- verify the SSH port is reachable;
- test from a standard SSH client.

### Useful logs

```cpp
globalSSHConfig.setDebugConfig(true, 115200);
globalSSHConfig.diagnoseSSHKeys();
```

## Related documents

- [Example firmware](../examples/README.md)
- [WiFi MAC vendor profiles](WIFI_MAC_VENDOR.md)
- [ESP32-C3 memory notes](ESP32_C3_MEMORY_NOTES.md)
- [Historical heartbeat measurement record](ESP32_C3_HEARTBEAT_MEASUREMENT.md)
- [SSH key authentication](SSH_KEYS_MEMORY.md)
- [RSA key authentication fix](RSA_KEY_AUTH_FIX.md)
- [Host-key verification](HOST_KEY_VERIFICATION.md)

---

**Documentation version:** 1.3
**Last update:** 2026-09-17
