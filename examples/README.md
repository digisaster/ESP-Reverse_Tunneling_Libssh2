# Example firmware

`src/main.cpp` is the reference reverse-tunnel firmware. It connects to Wi-Fi,
opens the SSH session, creates the remote listener, forwards channels, and
prints periodic diagnostics.

## Private configuration

Do not put credentials in `main.cpp` or `secrets.example.h`. Copy the template
once and edit only the ignored local file:

```powershell
if (!(Test-Path examples/src/secrets.h)) {
    Copy-Item examples/src/secrets.example.h examples/src/secrets.h
}
```

The following settings belong in `examples/src/secrets.h`:

- `WIFI_SSID` and `WIFI_PASSWORD`
- `SSH_HOST`, `SSH_PORT`, `SSH_USER`, and `SSH_PASSWORD`
- `TUNNEL1_REMOTE_PORT`, `TUNNEL1_LOCAL_HOST`, and `TUNNEL1_LOCAL_PORT`

From the repository root, verify that Git ignores the file:

```powershell
git check-ignore -v examples/src/secrets.h
git status --short
```

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
through the chip's USB-Serial/JTAG port. The C3 test listener uses
`TUNNEL1_REMOTE_PORT + 1` so it can run without colliding with a stale S2
listener:

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

## Single and multiple tunnels

The reference firmware defaults to the single mapping from `secrets.h`:

```text
127.0.0.1:TUNNEL1_REMOTE_PORT -> TUNNEL1_LOCAL_HOST:TUNNEL1_LOCAL_PORT
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
