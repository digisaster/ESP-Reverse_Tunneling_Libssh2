#ifndef SSH_CONFIG_H
#define SSH_CONFIG_H

#include "logger.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>

typedef void (*HostKeyMismatchCallback)(const String &expectedFingerprint,
                                        const String &actualFingerprint,
                                        const String &keyType, void *context);

// Structure for SSH configuration
struct SSHServerConfig {
  String host;
  int port;
  String username;
  String password;
  bool useSSHKey;
  String privateKeyData; // Private key content in memory
  String publicKeyData;  // Public key content in memory

  // Configuration known hosts
  bool verifyHostKey;                // Enable/disable verification
  String expectedHostKeyFingerprint; // Expected SHA256 fingerprint
  String hostKeyType; // Expected key type (ssh-ed25519, ssh-rsa, etc.)
  HostKeyMismatchCallback onHostKeyMismatch;
  void *hostKeyMismatchContext;

  // Default constructor
  SSHServerConfig()
      : host(""), port(22), username(""), password(""), useSSHKey(false),
        privateKeyData(""), publicKeyData(""), verifyHostKey(false),
        expectedHostKeyFingerprint(""), hostKeyType(""),
        onHostKeyMismatch(nullptr), hostKeyMismatchContext(nullptr) {}
};

// Structure for tunnel configuration
struct TunnelConfig {
  String remoteBindHost;
  int remoteBindPort;
  String localHost;
  int localPort;

  // Default constructor
  TunnelConfig()
      : remoteBindHost(""), remoteBindPort(0), localHost(""), localPort(0) {}
};

// Structure for connection management
struct ConnectionConfig {
  int keepAliveIntervalSec;
  int reconnectDelayMs;
  int maxReconnectAttempts;
  int connectionTimeoutSec;
  int bufferSize;
  int maxChannels;
  unsigned long channelTimeoutMs;
  bool libssh2KeepAliveEnabled;
  int libssh2KeepAliveIntervalSec;
  size_t tunnelRingBufferSize;
  int maxReverseListeners;

  // Default constructor
  ConnectionConfig()
      : keepAliveIntervalSec(10), reconnectDelayMs(5000),
        maxReconnectAttempts(5), connectionTimeoutSec(30), bufferSize(8192),
        maxChannels(10), channelTimeoutMs(30000), libssh2KeepAliveEnabled(true),
        libssh2KeepAliveIntervalSec(30), tunnelRingBufferSize(64 * 1024),
        maxReverseListeners(1) {}
};

// Structure for debug configuration
struct DebugConfig {
  bool debugEnabled;
  int serialBaudRate;
  LogLevel minLogLevel;

  // Default constructor
  DebugConfig()
      : debugEnabled(true), serialBaudRate(115200), minLogLevel(LOG_INFO) {}
};

// Main configuration class
class SSHConfiguration {
public:
  SSHConfiguration();
  ~SSHConfiguration();

  // SSH configuration methods
  void setSSHServer(const String &host, int port, const String &username,
                    const String &password);
  void setSSHKeyAuthFromMemory(const String &host, int port,
                               const String &username,
                               const String &privateKeyData,
                               const String &publicKeyData,
                               const String &passphrase = "");

  // Key validation/diagnostics for the in-memory authentication path.
  bool validateSSHKeys() const;
  void diagnoseSSHKeys() const;

  // Known hosts configuration methods
  void setHostKeyVerification(bool enable);
  void setExpectedHostKey(const String &fingerprint,
                          const String &keyType = "");
  void setHostKeyVerification(const String &fingerprint,
                              const String &keyType = "", bool enable = true);
  void setHostKeyMismatchCallback(HostKeyMismatchCallback callback,
                                  void *context = nullptr);

  // Tunnel configuration methods
  void setTunnelConfig(const String &remoteBindHost, int remoteBindPort,
                       const String &localHost, int localPort);
  void addTunnelMapping(const String &remoteBindHost, int remoteBindPort,
                        const String &localHost, int localPort);
  void addTunnelMapping(const TunnelConfig &mapping);
  bool removeTunnelMapping(size_t index);
  void clearTunnelMappings();

  // Connection configuration methods
  void setConnectionConfig(int keepAliveInterval, int reconnectDelay,
                           int maxReconnectAttempts, int connectionTimeout);
  void setBufferConfig(int bufferSize, int maxChannels, int channelTimeout,
                       size_t tunnelRingBufferSize = 64 * 1024);
  void setMaxReverseListeners(int maxListeners);
  void setKeepAliveOptions(bool enableLibssh2, int intervalSeconds);

  // Debug configuration methods
  void setDebugConfig(bool enabled, int baudRate);
  void setLogLevel(LogLevel level);

  // Getters to access configurations
  const SSHServerConfig &getSSHConfig() const { return sshConfig; }
  const TunnelConfig &getTunnelConfig(size_t index = 0) const;
  const std::vector<TunnelConfig> &getTunnelMappings() const {
    return tunnelMappings;
  }
  size_t getTunnelMappingCount() const { return tunnelMappings.size(); }
  const ConnectionConfig &getConnectionConfig() const {
    return connectionConfig;
  }
  const DebugConfig &getDebugConfig() const { return debugConfig; }
  HostKeyMismatchCallback getHostKeyMismatchCallback() const {
    return sshConfig.onHostKeyMismatch;
  }
  void *getHostKeyMismatchContext() const {
    return sshConfig.hostKeyMismatchContext;
  }

  // Validation methods
  bool validateConfiguration() const;
  void printConfiguration() const;

  // Thread-safe protection
  bool lockConfig() const;
  void unlockConfig() const;

private:
  SSHServerConfig sshConfig;
  std::vector<TunnelConfig> tunnelMappings;
  ConnectionConfig connectionConfig;
  DebugConfig debugConfig;

  // Semaphore for thread-safe protection
  SemaphoreHandle_t configMutex;

  // Private validation methods
  bool validateSSHConfig() const;
  bool validateTunnelConfig() const;
  bool validateConnectionConfig() const;
};

// Global configuration instance
extern SSHConfiguration globalSSHConfig;

#endif
