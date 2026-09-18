# Reference firmware

`examples/src/main.cpp` is the current `esp32tun` reference firmware. It
provisions WiFi and SSH settings, maintains one reverse SSH tunnel, registers
with Minis, and exposes periodic runtime diagnostics.

## Provisioning flow

If `/esp32tun.cfg` is missing or invalid, the device starts an open
`esp32tun-XXXXXX` access point. The captive portal normally opens
automatically; `http://192.168.4.1` is the fallback.

The WiFi page provides:

- detected or custom SSID;
- WiFi password with a show/hide eye button;
- MAC profile: `Original ESP32`, `Cisco`, or `HP`.

Only the first three MAC octets change. The final three are derived from the
factory device address. A changed profile is stored and applied after restart,
not while WiFi is active.

Once WiFi works, the Minis control plane starts. If no usable SSH identity is
stored, the firmware generates an ECDSA P-256 private/public key pair and stores
it in LittleFS.

The tunnel setup page then configures the SSH endpoint and one reverse mapping.
The operator can keep the generated key, provide another supported private key,
or use password authentication. Saving a complete configuration restarts the
device.

Normal tunnel operation does not leave either setup web server running.

## BOOT actions

- **3 short clicks:** WiFi-only reset; preserves SSH identity, tunnel settings,
  Minis configuration, and MAC profile.
- **Hold 4 seconds:** reopen tunnel configuration after restart.
- **5 short clicks:** full factory reset, including SSH keys and device
  configuration.

GPIO 9 is used by `esp32_c3_lowmem`; GPIO 0 is used by `lolin_s2_mini`.

## ESP32-C3 reference profile

Build and flash:

```powershell
pio run -e esp32_c3_lowmem
pio device list
pio run -e esp32_c3_lowmem --target upload --upload-port COM9
pio device monitor -e esp32_c3_lowmem --port COM9 --baud 115200
```

The profile intentionally uses:

```text
shared RX/TX transport work buffer: 2048 bytes
max active channels:                1
ring buffer per direction:          8192 bytes
prepend capacity per ring:          2048 bytes
```

The same 2 KB work buffer is reused because RX and TX pump phases are
sequential. The reference firmware intentionally configures one persisted
mapping; multi-listener coverage lives in the separate integration-test firmware.

For memory-sensitive changes, follow
[`../docs/ESP32_C3_MEMORY_NOTES.md`](../docs/ESP32_C3_MEMORY_NOTES.md).

## Minis `cfg.txt`

Heartbeat and managed configuration use:

```text
GET /hb/<sid>/cfg.txt
```

A complete managed configuration is:

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

`MAC_VENDOR` is optional and accepts `ORIGINAL`, `CISCO`, or `HP`. If omitted,
the locally stored selection is preserved.

The SSH username is always the lowercase eight-character SID. The SID is based
on the eFuse identity and therefore does not change when the WiFi MAC changes.

A valid changed configuration is persisted to `/esp32tun.cfg` and applied after
`ESP.restart()`. The firmware does not live-replace tunnel or MAC settings.

If the request fails, is malformed, or is deferred because of memory pressure,
the stored configuration and active SSH session are left untouched.

### Heartbeat interval

`HB_INTERVAL_MIN` is the base interval. A random delay from zero through the
same interval is added, so checks are spread between one and two times the base
interval.

### TLS memory protection

On the C3, a Minis TLS request starts only when both are available:

```text
free heap >= 70 KiB
largest free block >= 31 KiB
```

Otherwise the request is deferred. SSH has priority.

## SSH identity and uploads

The generated reference identity is ECDSA P-256 and is stored in:

```text
/esp32tun_ssh_key
/esp32tun_ssh_key.pub
```

When the public key is available, it is uploaded to:

```text
/hb/<sid>/ui/ssh_public_key.txt
```

The private key is never uploaded.

Manual key provisioning can use traditional RSA PEM or ECDSA P-256 EC PEM with
a matching OpenSSH public-key line. See
[`../docs/SSH_KEYS_MEMORY.md`](../docs/SSH_KEYS_MEMORY.md).

## Genesis

One `genesis.txt` inventory report is uploaded per SSH key identity. It includes
hardware and active network identity, for example:

```text
Hardware-MAC: AC:EB:E6:48:F9:C8
WiFi-MAC: 00:00:0C:48:F9:C8
MAC-Vendor: CISCO
```

Normal reboot, WiFi-only reset, changing networks, or changing MAC profile does
not generate another Genesis report. A full factory reset removes the SSH
identity; a newly generated identity therefore gets a new Genesis report.

## Expected runtime logs

MAC profile selection is visible during startup:

```text
[WIFI] MAC profile CISCO prepared: 00:00:0C:48:F9:C8
```

The Minis control plane then reports activity such as:

```text
[MINIS] SID: <sid>
[MINIS] Heartbeat/config GET: https://cloud.supcom.nl/hb/<sid>/cfg.txt
[MINIS] Fresh cfg.txt queued for comparison with stored settings
```

For the SSH side, normal operation reaches:

```text
Reverse listener ready ...
Tunnel State: Connected
```

An active forwarded connection changes `Active Channels` from `0` to `1`.
`Bytes Dropped` should remain `0`.

## Reverse listener behaviour

The reference mapping normally binds the remote listener to `127.0.0.1`, so it
is reachable from the SSH bastion without exposing the forwarded service on the
bastion's public interface.

After a hard reset or abrupt network loss, sshd can briefly retain the old
reverse listener. Normal retries generally recover after the stale server-side
session is reaped. Treat persistent failures as a server/session-cleanup issue
before adding more aggressive ESP reconnect behaviour.

## Security status

The current reference firmware still has two known hardening gaps:

- Minis HTTPS uses `setInsecure()`, so the TLS server certificate is not
  authenticated.
- SSH host-key verification is disabled in the reference firmware, although the
  underlying library supports it.

See [`../docs/HOST_KEY_VERIFICATION.md`](../docs/HOST_KEY_VERIFICATION.md).

## Other hardware profile

The WEMOS LOLIN S2 Mini can be built with:

```powershell
pio run -e lolin_s2_mini
```

The C3 profile above is the primary low-memory reference for current firmware
work.
