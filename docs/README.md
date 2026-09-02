# ESP-Reverse_Tunneling_Libssh2 Documentation

This documentation covers all aspects of the ESP-Reverse_Tunneling_Libssh2 library.

## 📖 Main Guides

### [SSH_KEYS_MEMORY.md](SSH_KEYS_MEMORY.md)
Complete guide for SSH key authentication with in-memory storage:

- SSH key configuration
- Secure LittleFS storage  
- Supported key formats
- Practical examples

### [HOST_KEY_VERIFICATION.md](HOST_KEY_VERIFICATION.md)
Security guide for host key verification:

- Protection against Man-in-the-Middle attacks
- Server fingerprint configuration
- Verification API
- Security best practices
- Migration and troubleshooting

## 🔧 Configuration

### Password authentication (simple)

```cpp
globalSSHConfig.setSSHServer("server.com", 22, "username", "password");
```

### SSH key authentication (recommended)

- [RSA key authentication fix and ESP32-C3 validation](RSA_KEY_AUTH_FIX.md)

```cpp
globalSSHConfig.setSSHKeyAuthFromMemory(
    "server.com", 22, "username", 
    privateKeyData, publicKeyData, ""
);
```

### Secure full configuration

```cpp
// SSH authentication
globalSSHConfig.setSSHKeyAuthFromMemory(
    "server.com", 22, "username",
    privateKeyData, publicKeyData, ""
);

// Server identity verification
globalSSHConfig.setHostKeyVerification(
    "SHA256:server_fingerprint",
    "ssh-ed25519",
    true
);

// Tunnel configuration
globalSSHConfig.setTunnelConfig(
    "127.0.0.1", 8080,    // Remote server bind
    "192.168.1.100", 80 // Local target (ESP32)
);
```

## 📊 Supported Key Formats

| Format | Compatibility | Recommendation |
|--------|---------------|----------------|
| Modern OpenSSH (`-----BEGIN OPENSSH PRIVATE KEY-----`) | ⚠️ Variable | Convert to PKCS#8 |
| PKCS#8 (`-----BEGIN PRIVATE KEY-----`) | ✅ Excellent | **Recommended** |
| PEM RSA (`-----BEGIN RSA PRIVATE KEY-----`) | ✅ Excellent | OK for RSA |
| PEM EC (`-----BEGIN EC PRIVATE KEY-----`) | ✅ Good | OK for ECDSA |

## 🔐 Supported Key Algorithms

| Algorithm | Support | Recommended Size |
|-----------|---------|------------------|
| **Ed25519** | ✅ Excellent | 256 bits (fixed) |
| RSA | ✅ Excellent | 4096 bits |
| ECDSA P-256 | ✅ Good | 256 bits |
| ECDSA P-384 | ✅ Good | 384 bits |
| ECDSA P-521 | ✅ Good | 521 bits |
| DSA | ⚠️ Deprecated | Not recommended |

## 🛡️ Security Levels

### Development (level 1)

```cpp
globalSSHConfig.setSSHServer("server.com", 22, "user", "password");
// No host verification
```

### Basic production (level 2)  

```cpp
globalSSHConfig.setSSHKeyAuthFromMemory(/* SSH keys */);
// Key-based auth but no host verification
```

### Secure production (level 3) - **Recommended**

```cpp
globalSSHConfig.setSSHKeyAuthFromMemory(/* SSH keys */);
globalSSHConfig.setHostKeyVerification(/* server fingerprint */);
// Key-based auth + host verification
```

## 🚀 Quick Start

### 1. Installation

```ini
# platformio.ini
lib_deps = 
    https://github.com/playmiel/ESP-Reverse_Tunneling_Libssh2.git
```

### 2. Minimal code

```cpp
#include "ESP-Reverse_Tunneling_Libssh2.h"

SSHTunnel tunnel;

void setup() {
    // WiFi configuration
    WiFi.begin("SSID", "PASSWORD");
    
    // SSH configuration
    globalSSHConfig.setSSHKeyAuthFromMemory(/* parameters */);
    
    // Initialization
    tunnel.init();
    tunnel.connectSSH();
}

void loop() {
    tunnel.loop();
}
```

### 3. Status check

```cpp
if (tunnel.isConnected()) {
    Serial.println("Tunnel active");
    Serial.printf("Active channels: %d\n", tunnel.getActiveChannels());
    Serial.printf("Bytes received: %lu bytes\n", tunnel.getBytesReceived());
    Serial.printf("Bytes sent: %lu bytes\n", tunnel.getBytesSent());
}
```

## 🔍 Troubleshooting

### Common issues

#### "Authentication failed"
- ✅ Check key format (prefer PKCS#8)
- ✅ Ensure public key is in `authorized_keys`
- ✅ Test manual SSH connection from a PC

#### "Host key verification failed"  
- ✅ Get the real server fingerprint
- ✅ Check configured fingerprint
- ✅ Ensure it's not an attack

#### "Connection timeout"
- ✅ Verify network connectivity
- ✅ Check SSH port open
- ✅ Test with a standard SSH client

### Useful logs
```cpp
// Enable detailed debug
globalSSHConfig.setDebugConfig(true, 115200);

// Diagnose SSH keys
globalSSHConfig.diagnoseSSHKeys();
```

## 📈 Performance Optimizations

### Memory
- Use Ed25519 keys (more compact)
- Adjust `bufferSize` according to usage
- Limit `maxChannels` to what you need

### Network
- Tune `keepAliveIntervalSec`
- Use built-in network optimizations and backpressure

### Recommended configuration
```cpp
globalSSHConfig.setConnectionConfig(
    30,    // Keep-alive: 30s
    5000,  // Reconnect delay: 5s
    10,    // Max reconnect attempts
    30     // Connection timeout: 30s
);

globalSSHConfig.setBufferConfig(
    8192,       // Buffer size: 8KB
    5,          // Max channels: 5
    1800000,    // Channel inactivity timeout: 30 minutes (0 = disabled)
    64 * 1024   // Ring buffer size per channel, per direction
);
```

The third `setBufferConfig` argument applies to each forwarded channel, not to
the outer SSH session. Activity in either direction resets the timer. A value
of `0` keeps idle forwarded channels open indefinitely. Use SSH keepalives
separately to detect a dead outer session.

### Multi-tunnel / multiple listeners

Use `addTunnelMapping()` and `setMaxReverseListeners()` to expose several
local services through a single SSH connection:

```cpp
globalSSHConfig.clearTunnelMappings();
globalSSHConfig.setMaxReverseListeners(3);

globalSSHConfig.addTunnelMapping("127.0.0.1", 22080, "192.168.1.100", 80);
globalSSHConfig.addTunnelMapping("127.0.0.1", 22081, "192.168.1.150", 502);
globalSSHConfig.addTunnelMapping("127.0.0.1", 22082, "192.168.1.200", 22);
```

Listeners bound to `127.0.0.1` are reachable only from the remote SSH server.
To use such a listener from another computer, first create a local forward
through that server, for example:

```bash
ssh -L 23181:127.0.0.1:22082 bastion-user@bastion.example.com
```

Keep that session open and run this in a second terminal:

```bash
ssh -p 23181 local-device-user@127.0.0.1
```

> **Note:** Listeners are created at `connectSSH()` time. Adding mappings
> while a session is active requires a reconnect to take effect.

## 📞 Support

For questions or issues:

1. Consult this documentation
2. Check the [examples/](../examples/)
3. Enable debug logs
4. Open a GitHub issue

---

**Documentation version:** 1.1
**Last update:** 2026-08-26
