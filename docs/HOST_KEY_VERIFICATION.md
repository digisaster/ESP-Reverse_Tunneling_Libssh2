# SSH host-key verification

The library supports SSH server fingerprint verification, but the current
`esp32tun` reference firmware does **not** enable it yet. The normal runtime
therefore logs:

```text
Host key verification disabled - connection accepted
```

This is a security-hardening item, not a tunnel-stability requirement.

## Current API

The available configuration methods are:

```cpp
globalSSHConfig.setHostKeyVerification(bool enable);
globalSSHConfig.setExpectedHostKey(fingerprint, keyType);
globalSSHConfig.setHostKeyVerification(fingerprint, keyType, true);
globalSSHConfig.setHostKeyMismatchCallback(callback, context);
```

A typical strict configuration is:

```cpp
globalSSHConfig.setHostKeyVerification(
    "SHA256:<base64-fingerprint>",
    "ssh-ed25519",
    true
);
```

The fingerprint may also be supplied as the 64-character SHA-256 hexadecimal
value. The optional key type can be used to require the expected host-key
algorithm.

## Obtaining the server fingerprint

On the SSH server, use the actual host public key, for example:

```bash
ssh-keygen -l -E sha256 -f /etc/ssh/ssh_host_ed25519_key.pub
```

Verify this value through a trusted administrative path before configuring the
ESP32.

## Discovery behaviour

If host-key verification is enabled but no expected fingerprint is configured,
the current library accepts the connection and prints both forms:

```text
Server fingerprint (SHA256 hex): ...
Server fingerprint (OpenSSH): SHA256:...
No expected fingerprint configured - accepting
```

This can be useful for initial discovery, but it is not persistent TOFU: the
library does not automatically store that fingerprint for future boots.
Production use should configure the verified fingerprint explicitly.

## Failure behaviour

When the configured key type or fingerprint does not match, the SSH connection
is rejected before authentication and listener creation. The optional mismatch
callback can be used for logging or telemetry; it does not override the failure.

Do not automatically accept a changed fingerprint. A legitimate SSH host-key
rotation should be verified separately and then deliberately updated in the
configuration.

## Reference-firmware status

The current reference firmware still leaves this feature disabled. Enabling it
later should be implemented as a provisioning/managed-configuration feature,
not by changing the proven SSH transport buffers or reconnect logic.

The Minis HTTPS client is a separate security concern: it currently uses
`setInsecure()`, so TLS traffic is encrypted but the Minis server certificate is
not authenticated.
