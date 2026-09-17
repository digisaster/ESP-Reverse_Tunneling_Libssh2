# SSH key authentication

The current `esp32tun` reference firmware uses device-local SSH keys stored in
LittleFS and loads them into memory for libssh2 authentication.

## Reference-firmware behaviour

When no usable local identity exists, the firmware automatically generates an
ECDSA P-256 key pair:

- private key: traditional `-----BEGIN EC PRIVATE KEY-----` PEM
- public key: matching `ecdsa-sha2-nistp256 ...` OpenSSH line

The files are stored as:

```text
/esp32tun_ssh_key
/esp32tun_ssh_key.pub
```

The public key may be uploaded to Minis. The private key never leaves the
ESP32.

A normal reboot, WiFi-only reset, MAC-vendor change, or managed tunnel update
preserves this identity. A full factory reset removes it and causes a new key
pair to be generated during setup.

## Manual provisioning

The tunnel setup page can also accept a private key supplied by the operator.
The known-good formats for the current firmware are:

| Key format | Status |
| --- | --- |
| Traditional RSA PEM | Hardware validated |
| ECDSA P-256 EC PEM + matching OpenSSH public key | Hardware validated |
| ECDSA P-384/P-521 | Accepted by setup validator; not hardware validated |
| PKCS#8 / modern OpenSSH private-key containers | Not systematically validated |
| Ed25519 client key | Not supported by the pinned mbedTLS authentication path |

For ECDSA P-256, provide the complete matching public-key line. The pinned
mbedTLS/libssh2 path cannot derive that OpenSSH public-key representation from
the EC private key during authentication.

## Library API

Applications using the library directly can authenticate with key material
already in memory:

```cpp
globalSSHConfig.setSSHKeyAuthFromMemory(
    host,
    port,
    username,
    privateKeyData,
    publicKeyData,
    passphrase
);
```

Legacy file-loading helpers still exist for library compatibility, but they are
not the normal provisioning path of the current reference firmware.

## Security

The stored private key is plain text in LittleFS. Treat physical flash access,
filesystem backups, and device images as credential access.

Do not commit real private keys, passphrases, provisioned LittleFS images, or
logs containing infrastructure credentials.

Use one device-specific key pair per ESP32 so an individual device can be
revoked without affecting others.

## RSA compatibility patch

The project applies a build-time compatibility patch to the pinned
`libssh2_esp` RSA implementation. This is still relevant because removing it
can reintroduce local RSA authentication failures before the server receives a
valid public-key request.

See [RSA key authentication fix](RSA_KEY_AUTH_FIX.md) for the defects and the
reason the patch is deliberately kept in the build.
