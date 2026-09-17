# ESP-Reverse_Tunneling_Libssh2

Arduino/ESP32 library and reference firmware for a persistent reverse SSH
tunnel built on libssh2.

The primary reference target is `esp32_c3_lowmem`: an ESP32-C3 without PSRAM,
configured for one reverse listener and one active forwarded channel. The
`lolin_s2_mini` environment remains available as a second hardware profile.

## Current ESP32-C3 baseline

The validated low-memory profile uses:

```text
shared transport work buffer: 2048 bytes
max active forwarded channels: 1
ring buffer per direction:     8192 bytes
prepend capacity per ring:     2048 bytes
SSH keepalive:                 30 seconds
```

A 10 MB forwarded transfer measured roughly 2.5 Mbit/s with zero dropped bytes.
The 8 KB directional rings and current TLS/keepalive rules are deliberate
stability constraints; see
[`docs/ESP32_C3_MEMORY_NOTES.md`](docs/ESP32_C3_MEMORY_NOTES.md) before changing
memory-sensitive transport code.

## Build and flash

For the ESP32-C3 reference target:

```powershell
pio run -e esp32_c3_lowmem
pio device list
pio run -e esp32_c3_lowmem --target upload --upload-port COM9
pio device monitor -e esp32_c3_lowmem --port COM9 --baud 115200
```

Replace `COM9` with the detected port.

On Windows, the repository also contains a build helper that checks the known
PIOArduino/RISC-V toolchain layout before compiling:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build-esp32-c3-windows.ps1 -Clean
```

If the local PlatformIO environment itself is suspect, use:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\diagnose-platformio.ps1
```

For the WEMOS LOLIN S2 Mini, use the `lolin_s2_mini` environment instead.

## First boot and provisioning

If `/esp32tun.cfg` is absent or invalid, the firmware starts an open
`esp32tun-XXXXXX` setup network. The captive portal normally opens
automatically; `http://192.168.4.1` is the fallback.

The WiFi page provides:

- detected or custom SSID;
- password with a show/hide eye button;
- MAC profile: `Original ESP32`, `Cisco`, or `HP`.

The MAC profile changes only the first three octets. The final three octets stay
device-specific. A changed profile is stored and becomes active after restart;
the firmware does not rewrite the station MAC while WiFi is running.

After WiFi becomes available, the firmware starts the Minis control plane. If
no usable local SSH identity exists, it automatically generates and stores an
ECDSA P-256 key pair. The public key can be uploaded to Minis; the private key
never leaves the ESP32.

The tunnel setup page configures the SSH server and one reverse mapping. It can
also accept a manually supplied password or private key. Saving the completed
configuration restarts the device and normal tunnel operation begins without a
web server left running.

Stored WiFi/SSH credentials and private keys are plain text in LittleFS. Treat
physical flash access and filesystem backups as credential access.

## BOOT button

Current reference-firmware behaviour:

- **3 short clicks:** clear WiFi credentials and reopen WiFi setup while
  preserving SSH keys, tunnel settings, Minis configuration, and MAC profile.
- **Hold 4 seconds:** restart into tunnel-configuration edit mode without
  clearing WiFi or SSH identity.
- **5 short clicks:** full factory reset, including device configuration, SSH
  keys, Minis configuration, and WiFi credentials.

The configured button is GPIO 9 on `esp32_c3_lowmem` and GPIO 0 on
`lolin_s2_mini`.

## Minis control plane

Heartbeat and managed configuration use the same short-lived HTTPS request:

```text
GET /hb/<sid>/cfg.txt
```

A managed configuration contains the heartbeat interval and complete tunnel
settings. `MAC_VENDOR` is optional:

```ini
HB_INTERVAL_MIN=3
TUNNEL_ENABLED=yes
SSH_HOST=192.168.0.101
SSH_PORT=22
REMOTE_BIND_HOST=127.0.0.1
REMOTE_BIND_PORT=23182
LOCAL_HOST=192.168.0.14
LOCAL_PORT=22
MAC_VENDOR=CISCO
```

Accepted MAC values are `ORIGINAL`, `CISCO`, and `HP`. If `MAC_VENDOR` is
absent, the locally stored profile is preserved.

The SSH username is always the lowercase eight-character SID. The SID comes
from the ESP32 eFuse identity, so changing the WiFi MAC does not change the SID
or SSH identity.

When managed tunnel or MAC settings change, the firmware writes the new
`/esp32tun.cfg` and restarts. Configuration is deliberately applied through the
normal boot path instead of live-replacing a working SSH session.

The ESP32-C3 starts Minis TLS only when there is at least 70 KiB total free heap
and a largest free block of at least 31 KiB. If memory is tighter, the request
is deferred and SSH remains untouched. A missed control-plane cycle during
heavy forwarded traffic is accepted behaviour.

## Public key and Genesis inventory

When a matching SSH public key is available, normal startup uploads:

```text
/hb/<sid>/ui/ssh_public_key.txt
```

The private key is never uploaded.

The firmware also uploads one `genesis.txt` inventory report per SSH key
identity. New reports include the factory MAC, active WiFi MAC, and selected MAC
profile, for example:

```text
Hardware-MAC: AC:EB:E6:48:F9:C8
WiFi-MAC: 00:00:0C:48:F9:C8
MAC-Vendor: CISCO
```

Normal reboot, WiFi-only reset, network change, or MAC-profile change does not
create another Genesis report. A full factory reset creates a new SSH identity
and therefore a new Genesis identity.

## SSH authentication

The current reference firmware automatically creates ECDSA P-256 credentials
when needed. Manual provisioning also supports the validated key paths:

- traditional unencrypted RSA PEM;
- ECDSA P-256 EC PEM with the matching OpenSSH public-key line.

Ed25519 client authentication is not available in the pinned mbedTLS backend.
See [`docs/SSH_KEYS_MEMORY.md`](docs/SSH_KEYS_MEMORY.md) for the current key
model and [`docs/RSA_KEY_AUTH_FIX.md`](docs/RSA_KEY_AUTH_FIX.md) before changing
or removing the pinned RSA compatibility patch.

## Security status

Two production-hardening items remain intentionally visible:

- Minis HTTPS currently uses `setInsecure()`: traffic is encrypted, but the
  Minis server certificate is not authenticated.
- The reference firmware currently leaves SSH host-key verification disabled.
  The library API supports fingerprint verification; see
  [`docs/HOST_KEY_VERIFICATION.md`](docs/HOST_KEY_VERIFICATION.md).

## Status LED

The reference profiles use an active-low onboard LED:

| Environment | LED pin |
| --- | ---: |
| `esp32_c3_lowmem` | GPIO 8 |
| `lolin_s2_mini` | GPIO 15 |

| State | Pattern |
| --- | --- |
| Setup/missing configuration | setup blink sequence |
| WiFi or SSH connecting | two short flashes every 2 seconds |
| Reverse tunnel connected | off |
| Tunnel disabled by Minis | off |
| Connection/authentication error | three short flashes every 2 seconds |

## Connecting through the reverse tunnel

The reference firmware normally binds the remote listener to `127.0.0.1`, so
the forwarded port is reachable on the SSH bastion but is not exposed directly
to the internet.

Example from the bastion:

```bash
ssh -p 23180 local-device-user@127.0.0.1
```

After a hard ESP reset or network loss, sshd can briefly retain the previous
reverse listener. The ESP retry path normally recovers once the old server-side
session is released. Persistent or minutes-long failures should be investigated
on the SSH server rather than worked around with aggressive ESP reconnects.

## Library use

The library can also be used independently of the reference provisioning
firmware:

```cpp
#include "ESP-Reverse_Tunneling_Libssh2.h"

SSHTunnel tunnel;

void setup() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  globalSSHConfig.setSSHServer(SSH_HOST, SSH_PORT, SSH_USER, SSH_PASSWORD);
  globalSSHConfig.setTunnelConfig("127.0.0.1", 23180,
                                  "192.168.1.1", 22);
  globalSSHConfig.setConnectionConfig(30, 5000, 5, 30);
  globalSSHConfig.setBufferConfig(8192, 5, 1800000, 64 * 1024);

  tunnel.init();
  tunnel.connectSSH();
}

void loop() {
  tunnel.loop();
}
```

The third `setBufferConfig` argument is the inactivity timeout per forwarded
channel in milliseconds. `0` disables channel inactivity closure.

## Documentation

- [`examples/README.md`](examples/README.md) — reference-firmware runtime and
  managed configuration
- [`docs/README.md`](docs/README.md) — technical documentation index
- [`docs/ESP32_C3_MEMORY_NOTES.md`](docs/ESP32_C3_MEMORY_NOTES.md) — C3
  anti-regression constraints
- [`docs/WIFI_MAC_VENDOR.md`](docs/WIFI_MAC_VENDOR.md) — MAC profiles
- [`docs/SSH_KEYS_MEMORY.md`](docs/SSH_KEYS_MEMORY.md) — SSH identity/key model
- [`docs/HOST_KEY_VERIFICATION.md`](docs/HOST_KEY_VERIFICATION.md) — host-key
  verification API
- [`test/README`](test/README) — test layout
- [`CHANGELOG.md`](CHANGELOG.md) — project history

## License

See [LICENSE](LICENSE).
