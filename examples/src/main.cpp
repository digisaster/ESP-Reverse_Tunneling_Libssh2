#include "ESP-Reverse_Tunneling_Libssh2.h"
#include "wifi_provisioning.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifndef ENABLE_MULTI_TUNNEL_DEMO
#define ENABLE_MULTI_TUNNEL_DEMO 0
#endif

static constexpr size_t CRITICAL_FREE_HEAP_BYTES = 12 * 1024;
static constexpr size_t CRITICAL_LARGEST_BLOCK_BYTES = 4 * 1024;

#ifndef SSH_TUNNEL_LOW_MEMORY_PROFILE
#define SSH_TUNNEL_LOW_MEMORY_PROFILE 0
#endif

#if SSH_TUNNEL_LOW_MEMORY_PROFILE && ENABLE_MULTI_TUNNEL_DEMO
#error "The low-memory profile supports only one tunnel mapping"
#endif

#if SSH_TUNNEL_LOW_MEMORY_PROFILE
static constexpr int TUNNEL_TRANSPORT_BUFFER_SIZE = 4096;
static constexpr int TUNNEL_MAX_CHANNELS = 1;
static constexpr size_t TUNNEL_RING_BUFFER_SIZE = 8 * 1024;
#else
static constexpr int TUNNEL_TRANSPORT_BUFFER_SIZE = 8192;
static constexpr int TUNNEL_MAX_CHANNELS = 5;
static constexpr size_t TUNNEL_RING_BUFFER_SIZE = 64 * 1024;
#endif

// SSH tunnel instance
SSHTunnel tunnel;
DeviceRuntimeConfig deviceConfig;
bool tunnelRuntimeReady = false;

// Monitoring variables
unsigned long lastStatsReport = 0;
const unsigned long STATS_INTERVAL = 10000; // 10 seconds

void connectWiFi();
void reportStats();
void configureSSHTunnel();
void configureMultiTunnelMappings();
void registerTunnelCallbacks();
const char *closeReasonToString(ChannelCloseReason reason);

// Simple event handlers to showcase the callback surface
void onSessionConnected();
void onSessionDisconnected();
void onChannelOpened(int channel);
void onChannelClosed(int channel, ChannelCloseReason reason);
void onTunnelError(int code, const char *detail);

void setup() {
  Serial.begin(115200);
  const unsigned long serialWaitStarted = millis();
  while (!Serial && millis() - serialWaitStarted < 2000) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  LOG_I(
      "MAIN",
      "ESP32 SSH Reverse Tunnel - Enhanced version with dynamic configuration");
#if SSH_TUNNEL_LOW_MEMORY_PROFILE
  LOG_I("MAIN", "Low-memory tunnel profile enabled");
#endif

  if (!wifi_provisioning::begin(deviceConfig)) {
    LOG_E("MAIN", "Unable to load or create WiFi configuration");
    return;
  }

  if (wifi_provisioning::isActive()) {
    LOG_I("MAIN", "WiFi setup mode active; tunnel startup is paused");
    return;
  }

  // WiFi connection
  connectWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    LOG_E("MAIN", "WiFi is unavailable; tunnel startup is paused");
    return;
  }

  if (!deviceConfig.setupComplete) {
    if (!wifi_provisioning::startDeviceSetup(deviceConfig)) {
      LOG_E("MAIN", "Unable to start tunnel setup page");
    }
    return;
  }

  // SSH tunnel configuration
  configureSSHTunnel();

  // SSH tunnel initialization
  if (!tunnel.init()) {
    LOG_E("MAIN", "Failed to initialize SSH tunnel");
    return;
  }
  tunnelRuntimeReady = true;

  // Start SSH connection
  if (!tunnel.connectSSH()) {
    LOG_E("MAIN", "Failed to connect SSH tunnel");
  }

  LOG_I("MAIN", "Setup completed successfully");
}

void loop() {
  wifi_provisioning::pollConfigResetButton();

  if (wifi_provisioning::isActive()) {
    wifi_provisioning::loop();
    return;
  }

  if (!tunnelRuntimeReady) {
    vTaskDelay(pdMS_TO_TICKS(100));
    return;
  }

  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    LOG_W("MAIN", "WiFi disconnected, reconnecting...");
    connectWiFi();
  }

  // SSH tunnel processing
  tunnel.loop();

  // Statistics report
  reportStats();

  // Use vTaskDelay instead of delay for better FreeRTOS compatibility
  vTaskDelay(pdMS_TO_TICKS(1));
}

void connectWiFi() {
  LOG_I("WIFI", "Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(deviceConfig.wifiSsid.c_str(), deviceConfig.wifiPassword.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    LOG_I("WIFI", "WiFi connected successfully");
    LOGF_I("WIFI", "IP address: %s", WiFi.localIP().toString().c_str());
    LOGF_I("WIFI", "Signal strength: %d dBm", WiFi.RSSI());
  } else {
    LOG_E("WIFI", "Failed to connect to WiFi");
  }
}

void configureSSHTunnel() {
  LOG_I("CONFIG", "Configuring SSH tunnel...");

  if (deviceConfig.sshAuthMethod == SSHAuthMethod::PrivateKey) {
    globalSSHConfig.setSSHKeyAuthFromMemory(
        deviceConfig.sshHost, deviceConfig.sshPort, deviceConfig.sshUsername,
        deviceConfig.sshPrivateKey, "", deviceConfig.sshKeyPassphrase);
    deviceConfig.sshPrivateKey = "";
  } else {
    globalSSHConfig.setSSHServer(deviceConfig.sshHost, deviceConfig.sshPort,
                                 deviceConfig.sshUsername,
                                 deviceConfig.sshPassword);
    deviceConfig.sshPassword = "";
  }

  // ===== METHOD 2: SSH configuration with key from LittleFS =====
  // This method automatically loads keys from LittleFS into memory
  // globalSSHConfig.setSSHKeyAuth(
  //   "your-remote-server.com",
  //   22,
  //   "your_username",
  //   "/ssh_key",       // Path to private key in LittleFS
  //   ""                // Passphrase for the key (optional)
  // );

  // ===== METHOD 3: SSH configuration with keys directly in memory =====
  // Example RSA private/public key placeholders (DO NOT USE IN PRODUCTION)
  /*
  String privateKey = "PLACEHOLDER_PRIVATE_KEY_EXAMPLE_DO_NOT_USE";
  String publicKey  = "ssh-rsa PLACEHOLDER_PUBLIC_KEY_EXAMPLE_DO_NOT_USE
  user@host"; // Minimal illustrative form
  globalSSHConfig.setSSHKeyAuthFromMemory(
    "your-remote-server.com",
    22,
    "your_username",
    privateKey,
    publicKey,
    ""  // Passphrase for the key (optional)
  );
  */

  // ===== METHOD 4: Load keys from LittleFS then use them in memory =====
  // Initialize LittleFS if not already done
  // if (!LittleFS.begin(true)) {
  //   LOG_E("CONFIG", "Failed to initialize LittleFS");
  //   return;
  // }

  // Manually load keys from LittleFS
  // if (globalSSHConfig.loadSSHKeysFromLittleFS("/ssh_key")) {
  //   LOG_I("CONFIG", "SSH keys loaded from LittleFS and stored in memory");
  // } else {
  //   LOG_E("CONFIG", "Failed to load SSH keys from LittleFS");
  // }

  if (ENABLE_MULTI_TUNNEL_DEMO) {
    configureMultiTunnelMappings();
  } else {
    globalSSHConfig.setTunnelConfig(
        deviceConfig.remoteBindHost, deviceConfig.remoteBindPort,
        deviceConfig.localHost, deviceConfig.localPort);
  }

#if SSH_TUNNEL_LOW_MEMORY_PROFILE
  globalSSHConfig.setMaxReverseListeners(1);
#endif

  // Connection configuration
  globalSSHConfig.setConnectionConfig(30,   // Keep-alive interval (seconds)
                                      5000, // Reconnection delay (ms)
                                      5,    // Max reconnection attempts
                                      30    // Connection timeout (seconds)
  );

  // Buffer configuration
  globalSSHConfig.setBufferConfig(TUNNEL_TRANSPORT_BUFFER_SIZE,
                                  TUNNEL_MAX_CHANNELS,
                                  1800000, // Channel timeout: 30 minutes
                                  TUNNEL_RING_BUFFER_SIZE);

  // Debug configuration
  globalSSHConfig.setDebugConfig(true,  // Debug enabled
                                 115200 // Serial baud rate
  );

  registerTunnelCallbacks();
  LOG_I("CONFIG", "Configuration complete");
}

void configureMultiTunnelMappings() {
  LOG_I("CONFIG", "Configuring multi-tunnel demo mappings");

  globalSSHConfig.clearTunnelMappings();
  globalSSHConfig.setMaxReverseListeners(3);

  globalSSHConfig.addTunnelMapping("127.0.0.1", 22080,   // Remote listener #1
                                   "192.168.1.100", 80); // HTTP cam

  globalSSHConfig.addTunnelMapping("127.0.0.1", 22081,    // Remote listener #2
                                   "192.168.1.150", 502); // Modbus TCP

  globalSSHConfig.addTunnelMapping("127.0.0.1", 22082,   // Localhost bind
                                   "192.168.1.200", 22); // SSH hop
}

void registerTunnelCallbacks() {
  SSHTunnelEvents events{};
  events.onSessionConnected = onSessionConnected;
  events.onSessionDisconnected = onSessionDisconnected;
  events.onChannelOpened = onChannelOpened;
  events.onChannelClosed = onChannelClosed;
  events.onError = onTunnelError;
  tunnel.setEventHandlers(events);
}

void reportStats() {
  unsigned long now = millis();
  if (now - lastStatsReport < STATS_INTERVAL) {
    return;
  }

  lastStatsReport = now;

  // Check heap state BEFORE logs
  size_t freeHeap = ESP.getFreeHeap();
  size_t minFreeHeap = ESP.getMinFreeHeap();
  size_t largestFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

  // Status report with memory verification
  if (freeHeap > 10000) { // Only if we have enough memory
    LOGF_I("STATS", "Tunnel State: %s", tunnel.getStateString().c_str());
    LOGF_I("STATS", "Active Channels: %d", tunnel.getActiveChannels());
    LOGF_I("STATS", "Bytes Sent: %lu", tunnel.getBytesSent());
    LOGF_I("STATS", "Bytes Received: %lu", tunnel.getBytesReceived());
    LOGF_I("STATS", "Bytes Dropped: %lu", tunnel.getBytesDropped());
  }

  // Throughput calculation (approximate)
  static unsigned long lastBytesSent = 0;
  static unsigned long lastBytesReceived = 0;

  unsigned long bytesSent = tunnel.getBytesSent();
  unsigned long bytesReceived = tunnel.getBytesReceived();
  unsigned long bytesDropped = tunnel.getBytesDropped();

  unsigned long sentRate = (bytesSent - lastBytesSent) * 1000 / STATS_INTERVAL;
  unsigned long receivedRate =
      (bytesReceived - lastBytesReceived) * 1000 / STATS_INTERVAL;

  if (freeHeap > 8000) { // Reduce logs if low memory
    LOGF_I("STATS", "Send Rate: %lu B/s", sentRate);
    LOGF_I("STATS", "Receive Rate: %lu B/s", receivedRate);
  }

  lastBytesSent = bytesSent;
  lastBytesReceived = bytesReceived;

  // WiFi info (essential)
  LOGF_I("WIFI", "RSSI: %d dBm", WiFi.RSSI());

  // Memory info (critical)
  LOGF_I("SYSTEM", "Free Heap: %d bytes (min: %d, largest: %d)", freeHeap,
         minFreeHeap, largestFreeBlock);
  LOGF_I("SYSTEM", "Uptime: %lu seconds", millis() / 1000);

  // Alert only when the remaining internal heap is genuinely critical. A
  // healthy ESP32-C3 can normally operate below the previous fixed 50KB
  // threshold.
  if (freeHeap < CRITICAL_FREE_HEAP_BYTES) {
    LOGF_W("MEMORY", "Critical free heap: %u bytes", (unsigned)freeHeap);
  }

  if (freeHeap >= CRITICAL_FREE_HEAP_BYTES &&
      largestFreeBlock < CRITICAL_LARGEST_BLOCK_BYTES) {
    LOGF_W("MEMORY", "Critical heap fragmentation: largest block %u bytes",
           (unsigned)largestFreeBlock);
  }
}

const char *closeReasonToString(ChannelCloseReason reason) {
  switch (reason) {
  case ChannelCloseReason::RemoteClosed:
    return "RemoteClosed";
  case ChannelCloseReason::LocalClosed:
    return "LocalClosed";
  case ChannelCloseReason::Error:
    return "Error";
  case ChannelCloseReason::Timeout:
    return "Timeout";
  case ChannelCloseReason::Manual:
    return "Manual";
  default:
    return "Unknown";
  }
}

void onSessionConnected() { LOG_I("CALLBACK", "SSH session established"); }

void onSessionDisconnected() { LOG_I("CALLBACK", "SSH session disconnected"); }

void onChannelOpened(int channel) {
  LOGF_I("CALLBACK", "Channel %d opened", channel);
}

void onChannelClosed(int channel, ChannelCloseReason reason) {
  LOGF_I("CALLBACK", "Channel %d closed (%s)", channel,
         closeReasonToString(reason));
}

void onTunnelError(int code, const char *detail) {
  LOGF_W("CALLBACK", "Tunnel error %d: %s", code, detail ? detail : "(none)");
}
