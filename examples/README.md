# Example firmware

`src/main.cpp` is the reference reverse-tunnel firmware. It connects to Wi-Fi,
opens the SSH session, creates the remote listener, forwards channels, and
prints periodic diagnostics.

## Device configuration

The firmware is configured entirely on the device. If `/esp32tun.cfg` does not
exist, it starts an open `esp32tun-XXXXXX` network. A captive portal normally
opens automatically; `http://192.168.4.1` is the fallback. The first page scans
for WiFi networks, tests the entered credentials, provides an eye button for
temporarily showing the WiFi password, and lets the operator select a MAC
vendor profile:

- `Original ESP32`
- `Cisco`
- `HP`

Only the first three octets of the WiFi station MAC change; the last three stay
device-specific. If the selected profile differs from the active profile, the
setting is stored and applied after a restart rather than being changed live
while the WiFi interface is running.

As soon as WiFi is available, the firmware starts the Minis/Control Center
control plane. This happens before SSH or reverse-tunnel setup is complete. The
device performs its SID bootstrap registration, starts the heartbeat/config
service, and begins fetching `/hb/<sid>/cfg.txt` while the second setup page can
still be active. A device can therefore appear in Minis and keep performing
control-plane checks even when SSH credentials are missing or the SSH server is
unavailable.

The temporary second page configures password or private-key SSH
authentication and one reverse tunnel. ECDSA authentication also requires the
matching OpenSSH public-key line because the bundled mbedTLS backend cannot
derive it from the private key. Saving restarts the board. Neither web server
runs during normal tunnel operation. Credentials are stored as plain text in
LittleFS and must be protected accordingly.

### BOOT button actions

The current reference firmware uses the BOOT button as follows:

- **3 short clicks:** WiFi-only reset. WiFi credentials are cleared and the
  first WiFi portal reopens. SSH keys, tunnel settings, Minis configuration,
  and the selected MAC vendor are preserved.
- **Hold for 4 seconds:** reopen the stored tunnel configuration after restart.
  WiFi credentials and MAC-vendor choice are preserved.
- **5 short clicks:** full factory reset. Device configuration, SSH key pair,
  Minis configuration, and WiFi credentials are removed. The next setup starts
  with `ORIGINAL` as the default MAC profile.

The configured button pin is GPIO 0 on the LOLIN S2 Mini and GPIO 9 on the
ESP32-C3 target.

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

The profile is deliberately limited to one active channel. Its explicit
transport/ring/prepend budget uses:

- 1 x 2 KB shared transport work buffer
- 2 x 8 KB directional ring buffers
- 2 x 2 KB prepend capacity

The read and write phases reuse the same 2 KB transport buffer sequentially.
Do not enable `ENABLE_MULTI_TUNNEL_DEMO` for this profile. Hardware validation
confirmed repeated channel close and reopen, 30-second keepalive messages, zero
dropped bytes, and heap recovery after channel closure.

The byte counters are cumulative for the lifetime of the firmware, including
channels that have already closed. `Bytes Dropped` counts payload that could
not be restored after a partial write and buffered payload abandoned during an
error close.

## Minis-managed tunnel configuration

The Minis control plane is independent of the SSH tunnel. Once WiFi is
available, the firmware registers the SID and starts its control-plane task.
After bootstrap, the periodic heartbeat and config retrieval are the same
request:

```text
GET /hb/<sid>/cfg.txt
```

There is no separate periodic `HEAD /ping`. The first config fetch is scheduled
shortly after the task starts; later checks follow the configured interval plus
jitter.

A complete managed configuration has this form:

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

`MAC_VENDOR` is optional. Accepted values are `ORIGINAL`, `CISCO`, and `HP`.
When omitted, the locally stored MAC profile is preserved. When present and
changed, it is persisted with the rest of the managed configuration and applied
on the next reboot before WiFi starts.

The SSH username is not supplied by `cfg.txt`; it is always the eight-character
lowercase SID. The SID is derived from the ESP32 eFuse identity and does not
change when the WiFi MAC vendor changes. The private key and optional passphrase
remain only in LittleFS and are never downloaded from or uploaded to Minis.
Managed activation is therefore accepted only when private-key authentication
is already configured locally.

When private-key authentication has a matching public key, normal startup also
uploads that **public key only** through the existing Minis upload route. It is
stored as:

```text
/hb/<sid>/ui/ssh_public_key.txt
```

The private key never leaves the ESP32. A successful upload is logged as:

```text
[MINIS] Public key uploaded for SID <sid> -> ui/ssh_public_key.txt
```

When a valid fetched configuration differs from the stored managed settings,
the firmware writes the updated configuration to `/esp32tun.cfg` and restarts.
The new configuration is applied through the normal boot path. There is no live
tunnel replacement, live MAC replacement, rollback state machine, or
control-plane pause/resume of SSH.

If `cfg.txt` is incomplete or malformed, or if the HTTPS request fails or is
deferred, the current stored configuration and SSH session are left untouched.

The ESP32-C3 low-memory profile protects the control plane with a TLS memory
guard. A Minis request is only started when total free heap is at least 70 KiB
and the largest free block is at least 31 KiB. When the guard defers a request,
the SSH session is left untouched.

The heartbeat interval is supplied by `HB_INTERVAL_MIN`. Scheduling deliberately
adds random jitter from zero up to the configured base interval, so the next
check occurs between one and two times the base interval.

## Genesis inventory

Normal startup uploads `ssh_public_key.txt` and, once per SSH key identity, a
`genesis.txt` inventory report. The Genesis marker uses the SSH public-key
key-tag, so normal reboots, WiFi-only resets, moving networks, and MAC-vendor
changes do not create a second Genesis report.

New Genesis reports include:

```text
Hardware-MAC: AC:EB:E6:48:F9:C8
WiFi-MAC: 00:00:0C:48:F9:C8
MAC-Vendor: CISCO
```

The hardware MAC is read from the factory/eFuse identity; the WiFi MAC is the
address currently exposed on the network. A factory reset generates a new SSH
identity, so a new Genesis report is expected after provisioning the reset
device.

For this proof-of-concept the Minis HTTPS clients, including the public-key and
Genesis uploads, still use `setInsecure()`. Traffic is encrypted but the remote
TLS certificate is not authenticated. SSH host-key verification is also still
disabled in the current reference firmware.

See `../docs/ESP32_C3_MEMORY_NOTES.md` for the current measured memory baseline
and `../docs/WIFI_MAC_VENDOR.md` for MAC-vendor behaviour and caveats.

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

With a configured vendor profile, WiFi startup includes a line similar to:

```text
[WIFI] MAC profile CISCO prepared: 00:00:0C:48:F9:C8
```

After WiFi succeeds, expect the Control Center sequence:

```text
[MINIS] WiFi available; starting control-center registration
[MINIS] SID: <sid>
[MINIS] Bootstrap GET: https://cloud.supcom.nl/hb/<sid>/
[MINIS] Control-center heartbeat/config service started
```

The periodic request then looks like:

```text
[MINIS] Heartbeat/config GET: https://cloud.supcom.nl/hb/<sid>/cfg.txt
[MINIS] HB_INTERVAL_MIN: ...
[MINIS] Fresh cfg.txt queued for comparison with stored settings
[MINIS] Next heartbeat/config check in ...
```

For a configured tunnel, wait for both messages before testing SSH forwarding:

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
