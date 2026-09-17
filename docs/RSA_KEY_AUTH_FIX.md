# RSA compatibility patch

Traditional unencrypted RSA-PEM authentication is hardware-validated on the
ESP32-C3 reference target. It depends on a compatibility patch applied to the
pinned `libssh2_esp` mbedTLS backend.

This document is retained because removing or weakening that patch can
reintroduce a known authentication regression.

## Why the patch exists

The pinned dependency required three RSA corrections:

1. The SSH `mpint` representation could require a leading prefix byte after the
   RSA modulus output buffer had already been sized, creating a one-byte buffer
   overflow.
2. A parsed private key was copied into an RSA context before that context was
   initialized with `mbedtls_rsa_init()`.
3. `_libssh2_mbedtls_pub_priv_key()` used an uninitialized `ret` value. An
   otherwise successful public-key derivation could therefore return an error
   before a valid authentication request reached the SSH server. The corrected
   path initializes this value to zero.

The third fix also matches the corresponding current upstream libssh2 behaviour.

## How the project protects the fix

`pio_extra/patch_libssh2_rsa.py` applies the corrections to PlatformIO's
downloaded dependency before compilation.

`library.json` registers the patcher as a library build extra script, so the fix
is also applied when this repository is installed through another project's
`lib_deps`.

The patcher deliberately checks for known source forms and should fail the build
if a future dependency version no longer matches the expected code. Do not make
it silently ignore an unknown dependency layout; that failure is the signal to
review whether the patch is still required or must be adapted.

## Validated scope

The following path is known to work on real ESP32-C3 hardware:

```text
traditional unencrypted RSA PEM
    -> parse private key
    -> derive/sign with RSA
    -> public-key authentication
    -> reverse listener
    -> forwarded tunnel traffic
```

ECDSA P-256 uses a different validated path and requires the matching OpenSSH
public-key line. Ed25519 client authentication is not supported by the pinned
mbedTLS authentication backend.

See [`SSH_KEYS_MEMORY.md`](SSH_KEYS_MEMORY.md) for the current reference-firmware
key model.

## Related keepalive rule

SSH keepalive is configured only after authentication. This ordering avoids
mixing pre-authentication keepalive traffic with authentication diagnostics.

The current ESP32-C3 stability rule that keepalive uses `want_reply=0` is
separately documented in
[`ESP32_C3_MEMORY_NOTES.md`](ESP32_C3_MEMORY_NOTES.md).

## Maintenance rule

When updating `libssh2_esp` or changing the mbedTLS backend:

1. inspect whether all three patched conditions still exist;
2. adapt or remove the patch only with source-level evidence;
3. rebuild both the repository and a consuming `lib_deps` project;
4. hardware-test RSA authentication through reverse-listener creation and real
   forwarded traffic.

Do not commit test private keys, passphrases, provisioned LittleFS images, or
credentials in diagnostic logs.
