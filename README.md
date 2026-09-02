# ESP-Reverse_Tunneling_Libssh2

Arduino library for creating reverse SSH tunnels from an ESP32 with libssh2.

The current reference target is a WEMOS LOLIN S2 Mini. It has been tested with
Wi-Fi, password authentication, a reverse SSH listener, an interactive SSH
channel, automatic reconnection, and a 45-minute uninterrupted idle session.

## Installation

Add the library to a PlatformIO project:

```ini
lib_deps =
  https://github.com/digisaster/ESP-Reverse_Tunneling_Libssh2.git
```

The library manifest installs the required `libssh2_esp` dependency.

## First-boot WiFi setup

The reference firmware no longer compiles the WiFi name and password into the
firmware. When `/esp32tun.cfg` is absent or invalid, the ESP32 starts a
temporary, open `esp32tun-XXXXXX` access point. It normally launches the setup
page automatically through a captive portal. The serial monitor also prints
`http://192.168.4.1` as a fallback.

Select a network and choose **Test and continue**. After WiFi is verified, the
open setup network closes. Reconnect to the selected network and follow the
displayed link to the temporary tunnel setup page.

The second page configures the SSH server, username, password or private key,
and one reverse-tunnel mapping. After saving, the ESP32 restarts and does not
create either setup server during normal operation. Existing WiFi-only files
automatically continue with this second phase.

Passwords and the optional private key are stored as plain text in LittleFS.
Treat physical flash access as credential access. SSH credentials are entered
only after the device has joined the trusted WiFi network.

### Reopen configuration

To replace an incorrect or outdated configuration, leave the firmware running
and hold the board's **BOOT** button for four seconds. Do not press RESET. The
reference firmware removes only `/esp32tun.cfg` and the stored SSH private key,
then restarts the open first-boot portal. This is enabled on GPIO 0 for the
LOLIN S2 Mini and GPIO 9 for the ESP32-C3 reference target.

## Build, flash, and monitor

Run these commands from the repository root:

```powershell
pio run -e lolin_s2_mini
pio device list
pio run -e lolin_s2_mini --target upload --upload-port COM9
pio device monitor -e lolin_s2_mini --port COM9 --baud 115200
```

Replace `COM9` with the port reported by `pio device list`. Close the serial
monitor with `Ctrl+C` before flashing, otherwise the port remains busy.

If automatic bootloader entry fails on an ESP32-S2:

1. Hold `BOOT` (or `0`).
2. Press and release `RESET`.
3. Release `BOOT`.
4. Run the upload command again.
5. Press `RESET` once after upload if the application does not start.

### ESP32-C3 low-memory target

The hardware-validated `esp32_c3_lowmem` environment targets an ESP32-C3
DevKitM-1 compatible board without PSRAM. It intentionally allows one listener
and one active forwarded channel, uses a 4 KB transport buffer, 8 KB per tunnel
direction, and a 4 KB prepend buffer. Native USB CDC is enabled for boards that
expose the ESP32-C3 USB-Serial/JTAG interface directly. The remote listener
port is now selected explicitly on the tunnel setup page.

```powershell
pio run -e esp32_c3_lowmem
pio device list
pio run -e esp32_c3_lowmem --target upload --upload-port COM9
pio device monitor -e esp32_c3_lowmem --port COM9 --baud 115200
```

Replace `COM9` with the detected port. Validation on the tested C3 covered:

1. Wi-Fi and reverse-listener establishment.
2. An active interactive forwarded SSH channel.
3. Repeated channel close and reopen.
4. Thirty-second keepalive messages and idle operation.
5. Zero dropped bytes and heap recovery after closing the channel.

The tested profile is suitable as the ESP32-C3 reference configuration.
Boards with a different flash layout or USB implementation may still need a
board-specific PlatformIO environment.

Do not publish LittleFS images or device backups containing credentials.

## Connecting through the reverse tunnel

The example binds its remote listener to `127.0.0.1`. This is intentional: the
forwarded port is reachable only from the bastion and is not exposed directly
to the internet.

From the bastion:

```bash
ssh -p 23180 local-device-user@127.0.0.1
```

From another computer, first forward a local port through the bastion:

```bash
ssh -p 22 -L 23181:127.0.0.1:23180 bastion-user@bastion.example.com
```

Keep that command running and connect in a second terminal:

```bash
ssh -p 23181 local-device-user@127.0.0.1
```

Wait for `Tunnel State: Connected` before opening the forwarded connection.
After a hard ESP32 reset, sshd may temporarily retain the old listener. For a
bastion, `ClientAliveInterval 15` and `ClientAliveCountMax 2` are recommended
to reap stale sessions promptly.

## Library usage

Configure Wi-Fi before starting the tunnel, then configure the SSH server and
one or more mappings:

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
channel in milliseconds. `1800000` is 30 minutes; `0` disables idle channel
closure. The outer SSH session keepalive is configured separately.

## Authentication and server verification

Password authentication is supported for initial testing. SSH key
authentication and host-key verification are recommended for production:

- [SSH key authentication](docs/SSH_KEYS_MEMORY.md)
- [Host-key verification](docs/HOST_KEY_VERIFICATION.md)

The example currently logs a warning when host-key verification is disabled.
Treat enabling verification as a required production-hardening step.

## Multiple tunnels

The provisioning page configures one mapping. Set `ENABLE_MULTI_TUNNEL_DEMO`
to `1` only when intentionally testing the hard-coded sample multi-listener
configuration. Applications can use `clearTunnelMappings()`,
`setMaxReverseListeners()`, and `addTunnelMapping()` to configure several
listeners before `connectSSH()`.

## Tested resource usage

Latest release builds:

| Environment | RAM | Flash |
| --- | ---: | ---: |
| `lolin_s2_mini` | 62,820 / 327,680 bytes (19.2%) | 1,161,070 / 1,310,720 bytes (88.6%) |
| `esp32_c3_lowmem` | 39,432 / 327,680 bytes (12.0%) | 1,226,524 / 1,310,720 bytes (93.6%) |

## Documentation

- [Example guide](examples/README.md)
- [Technical documentation](docs/README.md)
- [SSH key authentication](docs/SSH_KEYS_MEMORY.md)
- [Host-key verification](docs/HOST_KEY_VERIFICATION.md)
- [Integration tests](test/integration/README.md)
- [Changelog](CHANGELOG.md)

## License

See [LICENSE](LICENSE).
