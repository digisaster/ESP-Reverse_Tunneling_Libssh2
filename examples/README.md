# Example firmware

`src/main.cpp` is the reference reverse-tunnel firmware. It connects to Wi-Fi,
opens the SSH session, creates the remote listener, forwards channels, and
prints periodic diagnostics.

## Device configuration

The firmware is configured entirely on the device. If `/esp32tun.cfg` does not
exist, it starts an open `esp32tun-XXXXXX` network. A captive portal normally
opens automatically; `http://192.168.4.1` is the fallback. The first page scans
for WiFi networks and tests the entered credentials. It then closes the open
network and automatically tries to continue on the selected WiFi network.

The temporary second page configures password or private-key SSH
authentication and one reverse tunnel. Saving restarts the board. Neither web
server runs during normal tunnel operation. Credentials are stored as plain
text in LittleFS and must be protected accordingly.

To configure the device again, leave it running and hold **BOOT** for four
seconds without pressing RESET. The firmware removes the saved configuration
and private key and restarts the first-boot portal. The configured button pin
is GPIO 0 on the LOLIN S2 Mini and GPIO 9 on the ESP32-C3 target.

## WEMOS LOLIN S2 Mini

Use the root PlatformIO project for the tested S2 target:

```powershell
pio run -e lolin_s2_mini
pio device list
pio run -e lolin_s2_mini --target upload --upload-port COM9
pio device monitor -e lolin_s2_mini --port COM9 --baud 115200
```

Replace `COM9` with the current device port and close the monitor with `Ctrl+C`
before uploading.

If manual download mode is required, hold `BOOT`, press and release `RESET`,
then release `BOOT` and retry the upload. Press `RESET` once after flashing if
the application does not start automatically.

## ESP32-C3 low-memory profile

The hardware-validated `esp32_c3_lowmem` environment uses the generic
`esp32-c3-devkitm-1` board definition for a 4 MB ESP32-C3 board without PSRAM.
Native USB CDC is enabled so serial output is visible on boards that connect
through the chip's USB-Serial/JTAG port. Select the required remote listener
port on the tunnel setup page:

```powershell
pio run -e esp32_c3_lowmem
pio device list
pio run -e esp32_c3_lowmem --target upload --upload-port COM9
pio device monitor -e esp32_c3_lowmem --port COM9 --baud 115200
```

The profile is deliberately limited to one active channel. Its buffer budget
is approximately 32 KB before libssh2, Wi-Fi, and allocator overhead:

- 2 x 4 KB transport buffers
- 2 x 8 KB directional ring buffers
- 2 x 4 KB prepend buffers

Do not enable `ENABLE_MULTI_TUNNEL_DEMO` for this profile. Hardware validation
confirmed repeated channel close and reopen, 30-second keepalive messages, zero
dropped bytes, and heap recovery after channel closure. A continually falling
`Min Free Heap` is expected; a continually falling current `Free Heap` across
repeated cycles is not and may indicate a leak.

The byte counters are cumulative for the lifetime of the firmware, including
channels that have already closed. `Bytes Dropped` counts payload that could
not be restored after a partial write and buffered payload abandoned during an
error close. Normal C3 operation below 50 KB free heap no longer produces a
warning; warnings are reserved for critically low usable heap.

## Single and multiple tunnels

The reference firmware uses the single mapping saved by the setup page:

```text
REMOTE_BIND_HOST:REMOTE_BIND_PORT -> LOCAL_HOST:LOCAL_PORT
```

Because the listener binds to remote `127.0.0.1`, it is reachable only from
the SSH bastion. This prevents accidental public exposure.

`ENABLE_MULTI_TUNNEL_DEMO` defaults to `0`. Set it to `1` only to run the
hard-coded sample mappings in `configureMultiTunnelMappings()`. Applications
should normally build mappings from their own configuration before
`connectSSH()`.

## Channel inactivity

The normal S2 profile configures:

```cpp
globalSSHConfig.setBufferConfig(8192, 5, 1800000, 64 * 1024);
```

The third argument is the forwarded-channel inactivity timeout in
milliseconds. The example uses 30 minutes. Set it to `0` to disable idle
channel closure. This setting is independent of the outer SSH keepalive.

The 30-minute setting has been validated with an uninterrupted 45-minute
interactive SSH session on the LOLIN S2 Mini.

## Expected serial output

Wait for both messages before testing the tunnel:

```text
Reverse listener ready ...
Tunnel State: Connected
```

An active forwarded connection changes `Active Channels` from `0` to `1`.
After a hard reset, an old sshd session may retain the remote port briefly;
automatic retries normally recover after sshd releases the stale listener.

## Callbacks

The example registers `SSHTunnelEvents` callbacks for session connect and
disconnect, channel open and close, and tunnel errors. Replace their logging
implementations with application-specific handling when integrating the
library.
