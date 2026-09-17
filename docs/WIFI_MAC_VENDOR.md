# WiFi MAC vendor profiles

The `esp32tun` reference firmware can optionally change the vendor prefix of the
WiFi station MAC address while keeping the device-specific final three octets.
This is a reference-firmware feature; it does not change the SSH transport
library itself.

## Available profiles

| Profile | Prefix | Result example for hardware MAC `AC:EB:E6:48:F9:C8` |
| --- | --- | --- |
| `ORIGINAL` | Factory ESP32 prefix | `AC:EB:E6:48:F9:C8` |
| `CISCO` | `00:00:0C` | `00:00:0C:48:F9:C8` |
| `HP` | `3C:D9:2B` | `3C:D9:2B:48:F9:C8` |

Only the first three octets are replaced. The last three octets are copied from
the ESP32 factory MAC, so each device keeps its device-specific suffix.

The selected MAC is prepared before WiFi is initialized. Runtime reconnects do
not rewrite the MAC while the interface is active.

## WiFi setup page

The first-boot/captive-portal WiFi page contains a **MAC vendor** selector with:

- Original ESP32
- Cisco
- HP

The WiFi password field also contains an eye button that toggles between hidden
and visible text in the browser. This affects only the page display; the stored
credential format is unchanged.

WiFi credentials are tested before they are stored. If the selected vendor
profile differs from the currently active profile, the new setting is stored
and the device restarts. The selected MAC then becomes active on the next boot.
This avoids changing the station MAC while WiFi is already running.

## Local persistent configuration

Device configuration version 4 stores the selected profile in `/esp32tun.cfg`:

```ini
mac_vendor=ORIGINAL
```

or:

```ini
mac_vendor=CISCO
```

or:

```ini
mac_vendor=HP
```

Configuration files from versions 1 through 3 remain readable. Because those
versions did not contain a MAC-vendor setting, they are interpreted as
`ORIGINAL`.

## Minis `cfg.txt`

`MAC_VENDOR` is optional in the managed Minis configuration:

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

Accepted values are:

```text
ORIGINAL
CISCO
HP
```

If `MAC_VENDOR` is omitted, the ESP32 keeps the locally stored MAC profile.
If a valid `MAC_VENDOR` differs from the stored setting, the complete managed
configuration is persisted and the ESP32 restarts. The new MAC is applied
through the normal boot path; there is no live MAC replacement.

An invalid `MAC_VENDOR` makes the fetched managed configuration invalid and the
current stored configuration remains active.

## Minis SID and SSH identity

Changing the WiFi MAC does **not** change the Minis SID. The SID is derived from
`ESP.getEfuseMac()` rather than from the active WiFi interface address.

Changing the MAC vendor also does not regenerate the SSH key. A device can
therefore appear on the local network with a Cisco or HP prefix while keeping
the same:

- Minis SID
- SSH username derived from that SID
- stored SSH key pair
- Genesis key tag

## BOOT-button behaviour

The MAC-vendor setting follows the same persistence rules as the rest of the
WiFi/device configuration:

- **3 short BOOT clicks:** WiFi-only reset. Stored SSH/tunnel configuration,
  SSH keys, Minis configuration, and the selected MAC vendor are preserved.
- **Hold BOOT for 4 seconds:** reopen the stored tunnel configuration. WiFi and
  MAC-vendor settings are preserved.
- **5 short BOOT clicks:** full factory reset. Device configuration and SSH
  keys are removed; the next setup starts with `ORIGINAL` as the default MAC
  profile.

## Genesis inventory

New Genesis reports record both the factory identity and the address actually
used on WiFi:

```text
Hardware-MAC: AC:EB:E6:48:F9:C8
WiFi-MAC: 00:00:0C:48:F9:C8
MAC-Vendor: CISCO
```

Genesis is still one-time per SSH key identity. Changing only the MAC vendor
does not create another Genesis file. Existing Genesis reports are not rewritten
retroactively. A new Genesis report is produced after a factory reset when a new
SSH identity is generated.

## Operational notes

The Cisco and HP profiles intentionally use prefixes associated with those
vendors so ordinary network inventory tools may identify the ESP32 as that
vendor. This is different from using a locally administered MAC prefix.

Use these profiles only on networks where this behaviour is intentional. NAC,
asset inventory, DHCP policy, monitoring, or vendor-based firewall rules may
react to the spoofed vendor identity. The firmware preserves the ESP32-specific
last three octets, but administrators should still ensure that the resulting
full MAC address does not duplicate another device on the same Layer-2 network.

The feature is independent of reverse SSH forwarding. No SSH transport buffer,
channel, keepalive, or reconnect behaviour is changed by selecting a MAC vendor.
