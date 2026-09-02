# RSA key authentication fix and validation

## Status

RSA private-key authentication from memory is part of the proposed
`esp32tun` **1.0.0-beta.1** reference-firmware baseline. It was validated on
real ESP32-C3 hardware using the `esp32_c3_lowmem` environment, an unencrypted
traditional RSA PEM private key, and a matching `authorized_keys` entry on an
OpenSSH server.

This result does not yet claim compatibility with every key container or key
algorithm. RSA-PEM is the known-good baseline for subsequent tests.

## Observed failure

The TCP connection and SSH handshake completed, but
`libssh2_userauth_publickey_frommemory()` returned `-1` with an unknown error.
The server logged a disconnect during `preauth` and never logged a rejected
public key. RSA parsing and signing diagnostics were also absent.

That combination showed that authentication failed locally while libssh2 was
deriving the public-key blob from the supplied private key, before an
authentication request reached the server.

## Root causes in the pinned dependency

The project pins `libssh2_esp` 1.1. Its mbedTLS RSA path required three
compatibility corrections:

1. The RSA modulus gained an SSH `mpint` prefix byte after the output buffer
   had been sized, causing a one-byte write beyond the allocation.
2. A parsed private key was copied into an RSA context before that context was
   initialized with `mbedtls_rsa_init()`.
3. `_libssh2_mbedtls_pub_priv_key()` declared `int ret;` without initializing
   it. On an otherwise successful path, `if(ret)` therefore read an undefined
   value and could return `-1` before sending the public key. Initializing it
   as `int ret = 0;` resolved the final reproducible failure.

The third correction matches the current upstream libssh2 implementation,
which initializes the result to zero.

## Project implementation

`pio_extra/patch_libssh2_rsa.py` applies the corrections to PlatformIO's
downloaded dependency before compilation. The patcher is deliberately strict:
it accepts the known vulnerable or corrected source forms and aborts the build
if the pinned dependency no longer matches. This prevents a future dependency
update from being modified silently at the wrong location.

The patch also retains temporary, non-secret RSA diagnostics for compatibility
testing. They report parser return codes, key type and size, and signing
success or failure. They never print the private key, passphrase, public-key
contents, or signature.

Keepalive configuration was moved until after authentication. Before that
change, OpenSSH logged a pre-authentication SSH global request as
`dispatch_protocol_error: type 80`; this message was diagnostic noise rather
than the key-authentication cause.

## Validated result

The successful ESP32-C3 test established all of the following:

- WiFi connection and SSH handshake;
- RSA private-key parsing and RSA context validation;
- RSA signing from the key stored in LittleFS and loaded into memory;
- public-key authentication without an SSH account password;
- reverse-listener creation and usable tunnel operation.

The reference build remained within the C3 profile limits at approximately
12.0% RAM and 93.6% flash usage.

## Compatibility baseline and next tests

| Key or container | Beta status |
| --- | --- |
| Unencrypted traditional RSA PEM | Hardware validated |
| RSA PEM with passphrase | Not yet validated |
| PKCS#8 PEM | Not yet validated |
| OpenSSH RSA private-key container | Not yet validated |
| ECDSA | Not enabled in the pinned ESP32 mbedTLS key path |
| Ed25519 | Not supported by the pinned mbedTLS key path |

Modern-key testing should proceed one variable at a time, beginning with a
2048-bit RSA key in the known-good traditional PEM container. For each case,
record the key algorithm, key size, container, encryption/passphrase status,
ESP diagnostic result, and corresponding OpenSSH server log.

Do not commit test private keys, passphrases, provisioned LittleFS images, or
serial logs containing infrastructure credentials.
