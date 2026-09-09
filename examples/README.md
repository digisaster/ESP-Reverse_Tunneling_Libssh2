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

As soon as that WiFi connection succeeds, the firmware starts the Minis/Control
Center control plane. This happens before SSH or reverse-tunnel setup is
complete. The device performs its SID bootstrap registration, starts the
heartbeat/config service, and begins fetching `/hb/<sid>/cfg.txt` while the
second setup page can still be active. A device can therefore appear in Minis
and keep sending heartbeats even when SSH credentials are missing or the SSH
server is unavailable.

The temporary second page configures password or private-key SSH
authentication and one reverse tunnel. ECDSA authentication also requires the
matching OpenSSH public-key line because the bundled mbedTLS backend cannot
derive it from the private key. Saving restarts the board. Neither web server
runs during normal tunnel operation. Credentials are stored as plain text in
LittleFS and must be protected accordingly.

To edit the tunnel configuration without losing WiFi or SSH credentials, press
**BOOT** three times within two seconds. The firmware restarts, reconnects to
the stored WiFi network, and opens the tunnel setup page at the IP address in
the serial log. Hidden password and key fields keep their stored values when
left empty. This edit path requires the stored WiFi network to be reachable.
When replacing a private key, submit its matching public key in the same form
to avoid retaining a mismatched key pair.

For a complete reset or recovery when the stored WiFi is no longer usable,
leave the device running and hold **BOOT** for four seconds without pressing
RESET. The firmware removes the saved configuration and private/public key pair
and restarts the first-boot `esp32tun-XXXXXX` portal. The configured button pin
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

## Minis-managed tunnel configuration

The Minis control plane is independent of the SSH tunnel. Once WiFi is
available, the firmware registers the SID, starts its heartbeat task, and
requests `/hb/<sid>/cfg.txt`. The first config fetch is scheduled shortly after
the control-plane task starts; subsequent heartbeat/config checks follow the
configured interval plus jitter.

A complete managed configuration has this form:

```ini
HB_INTERVAL_MIN=2
TUNNEL_ENABLED=yes
SSH_HOST=edp.supcom.nl
SSH_PORT=443
REMOTE_BIND_HOST=127.0.0.1
REMOTE_BIND_PORT=23181
LOCAL_HOST=192.168.19.10
LOCAL_PORT=22
```

The SSH username is not supplied by `cfg.txt`; it is always the eight-character
lowercase SID. The private key and optional passphrase remain only in LittleFS
and are never downloaded from or uploaded to Minis. Managed activation is
therefore accepted only when private-key authentication is already configured
locally.

A configuration may be fetched and cached before SSH provisioning is complete.
Tunnel activation remains a separate step and requires a locally available
private key. Failure to establish SSH does not stop the Minis heartbeat/config
service.

When private-key authentication has a matching public key, normal startup also
uploads that **public key only** through the existing Minis `uploot.php` route.
It is stored as:

```text
/hb/<sid>/ui/ssh_public_key.txt
```

The upload uses `dest=ui`, so a later startup can replace the same public-key
file. The private key never leaves the ESP32. A successful upload is logged as:

```text
[MINIS] Public key uploaded for SID <sid> -> ui/ssh_public_key.txt
```

Hardware validation on ESP32-C3 confirmed the complete flow with SID
`48e6ebac`: the public key was uploaded, Minis supplied a managed configuration
for `48e6ebac@edp.supcom.nl:443`, public-key authentication succeeded, and the
managed reverse listener `127.0.0.1:23182 -> 192.168.19.10:22` was activated
and stored.

When a valid fetched configuration differs from the active settings, the
firmware stops the current tunnel and tries the new SSH session and listener.
It stores the new settings only after activation succeeds. On failure it
restores and reconnects the previous configuration. `TUNNEL_ENABLED=no`
cleanly stops the tunnel while WiFi and the heartbeat service remain active.

The heartbeat interval is supplied by `HB_INTERVAL_MIN`. Scheduling deliberately
adds random jitter from zero up to the configured base interval, so the next
check occurs between one and two times the base interval. This avoids many
devices contacting Minis simultaneously.

For this proof-of-concept the Minis HTTPS clients, including the public-key
upload, still use `setInsecure()`. Traffic is encrypted but the remote TLS
certificate is not authenticated. No private key is sent through this channel.
Certificate validation is required before treating Minis transport as
production-hardened. SSH host-key verification is also still disabled in the
current reference firmware and remains a separate production-hardening item.

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

Immediately after first-boot WiFi succeeds, expect the Control Center sequence
to begin even before SSH setup is complete:

```text
[MINIS] WiFi available; starting control-center registration
[MINIS] SID: <sid>
[MINIS] Bootstrap GET: https://cloud.supcom.nl/hb/<sid>/
[MINIS] Control-center heartbeat/config service started
```

The first `cfg.txt` fetch follows shortly afterward. For a configured tunnel,
wait for both messages before testing SSH forwarding:

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
