#include "ESP-Reverse_Tunneling_Libssh2.h"
#include "minis_public_key_upload.h"
#include "minis_registration.h"
#include "ssh_key_provisioning.h"
#include "status_led.h"
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

SSHTunnel tunnel;
DeviceRuntimeConfig deviceConfig;
bool tunnelRuntimeReady = false;
bool minisBootstrapAttempted = false;
bool minisHeartbeatStartAttempted = false;
bool minisHeartbeatStarted = false;
bool automaticIdentityAttempted = false;
bool automaticPublicKeyUploadAttempted = false;
unsigned long lastStatsReport = 0;
const unsigned long STATS_INTERVAL = 10000;

void connectWiFi();
void ensureMinisControlPlane();
void reportStats();
void configureSSHTunnel();
void applyPendingMinisConfig();
void configureMultiTunnelMappings();
void registerTunnelCallbacks();
const char *closeReasonToString(ChannelCloseReason reason);
void onSessionConnected();
void onSessionDisconnected();
void onChannelOpened(int channel);
void onChannelClosed(int channel, ChannelCloseReason reason);
void onTunnelError(int code, const char *detail);

void setup() {
  status_led::begin();
  Serial.begin(115200);
  const unsigned long serialWaitStarted = millis();
  while (!Serial && millis() - serialWaitStarted < 2000)
    vTaskDelay(pdMS_TO_TICKS(10));

  LOG_I("MAIN", "ESP32 SSH Reverse Tunnel - Enhanced version with dynamic configuration");
#if SSH_TUNNEL_LOW_MEMORY_PROFILE
  LOG_I("MAIN", "Low-memory tunnel profile enabled");
#endif

  if (!wifi_provisioning::begin(deviceConfig)) {
    status_led::set(status_led::State::Error);
    LOG_E("MAIN", "Unable to load or create WiFi configuration");
    return;
  }
  if (wifi_provisioning::isActive()) {
    status_led::set(status_led::State::MissingConfig);
    LOG_I("MAIN", "WiFi setup mode active; tunnel startup is paused");
    return;
  }

  status_led::set(status_led::State::Connecting);
  connectWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    status_led::set(status_led::State::Error);
    LOG_E("MAIN", "WiFi is unavailable; tunnel startup is paused");
    return;
  }

  ensureMinisControlPlane();

  if (!deviceConfig.setupComplete || wifi_provisioning::editRequested()) {
    status_led::set(status_led::State::Setup);
    if (!wifi_provisioning::startDeviceSetup(deviceConfig)) {
      status_led::set(status_led::State::Error);
      LOG_E("MAIN", "Unable to start tunnel setup page");
    }
    return;
  }

  if (deviceConfig.sshAuthMethod == SSHAuthMethod::PrivateKey &&
      !deviceConfig.sshPublicKey.isEmpty() &&
      !automaticPublicKeyUploadAttempted) {
    automaticPublicKeyUploadAttempted = true;
    if (!minis_public_key_upload::upload(deviceConfig.sshPublicKey))
      LOG_W("MAIN", "Minis public key upload did not complete");
  }

  configureSSHTunnel();
  if (!tunnel.init()) {
    status_led::set(status_led::State::Error);
    LOG_E("MAIN", "Failed to initialize SSH tunnel");
    return;
  }
  tunnelRuntimeReady = true;
  if (!deviceConfig.tunnelEnabled) {
    status_led::set(status_led::State::Disabled);
    LOG_I("MAIN", "Tunnel is disabled by stored managed configuration");
  } else if (!tunnel.connectSSH()) {
    status_led::set(status_led::State::Error);
    LOG_E("MAIN", "Failed to connect SSH tunnel");
  }
  LOG_I("MAIN", "Setup completed successfully");
}

void loop() {
  status_led::update();
  wifi_provisioning::pollConfigResetButton();
  if (wifi_provisioning::isActive()) {
    wifi_provisioning::loop();
    // As soon as first-boot WiFi provisioning succeeds, bring up the Minis
    // control plane and provision a local SSH identity while the device setup
    // page is still active.
    ensureMinisControlPlane();
    return;
  }
  if (!tunnelRuntimeReady) {
    ensureMinisControlPlane();
    vTaskDelay(pdMS_TO_TICKS(100));
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    LOG_W("MAIN", "WiFi disconnected, reconnecting...");
    connectWiFi();
  }
  ensureMinisControlPlane();
  applyPendingMinisConfig();
  tunnel.loop();
  reportStats();
  vTaskDelay(pdMS_TO_TICKS(1));
}

void connectWiFi() {
  status_led::set(status_led::State::Connecting);
  LOG_I("WIFI", "Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(deviceConfig.wifiSsid.c_str(), deviceConfig.wifiPassword.c_str());
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    const unsigned long waitStarted = millis();
    while (millis() - waitStarted < 1000) {
      status_led::update();
      wifi_provisioning::pollConfigResetButton();
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    LOG_I("WIFI", "WiFi connected successfully");
    LOGF_I("WIFI", "IP address: %s", WiFi.localIP().toString().c_str());
    LOGF_I("WIFI", "Signal strength: %d dBm", WiFi.RSSI());
  } else {
    status_led::set(status_led::State::Error);
    LOG_E("WIFI", "Failed to connect to WiFi");
  }
}

void ensureMinisControlPlane() {
  if (WiFi.status() != WL_CONNECTED)
    return;

  if (!minisBootstrapAttempted) {
    minisBootstrapAttempted = true;
    LOG_I("MINIS", "WiFi available; starting control-center registration");
    if (!minis_registration::registerClient())
      LOG_W("MAIN", "Minis registration attempt did not complete");
  }

  if (!minisHeartbeatStartAttempted) {
    minisHeartbeatStartAttempted = true;
    minisHeartbeatStarted = minis_registration::startHeartbeatTask();
    if (minisHeartbeatStarted)
      LOG_I("MINIS", "Control-center heartbeat/config service started");
    else
      LOG_W("MAIN", "Minis heartbeat service could not be started");
  }

  // Preserve existing complete/manual configurations. Automatic identity
  // provisioning is only part of first-time onboarding.
  if (!deviceConfig.setupComplete && !automaticIdentityAttempted) {
    automaticIdentityAttempted = true;
    if (!ssh_key_provisioning::ensure(deviceConfig)) {
      LOG_W("MAIN", "Automatic SSH identity provisioning did not complete");
    }
  }

  if (!deviceConfig.setupComplete &&
      deviceConfig.sshAuthMethod == SSHAuthMethod::PrivateKey &&
      !deviceConfig.sshPublicKey.isEmpty() &&
      !automaticPublicKeyUploadAttempted) {
    automaticPublicKeyUploadAttempted = true;
    if (!minis_public_key_upload::upload(deviceConfig.sshPublicKey))
      LOG_W("MAIN", "Automatic Minis public key upload did not complete");
  }
}

void configureSSHTunnel() {
  LOG_I("CONFIG", "Configuring SSH tunnel...");
  if (deviceConfig.sshAuthMethod == SSHAuthMethod::PrivateKey) {
    globalSSHConfig.setSSHKeyAuthFromMemory(deviceConfig.sshHost,
        deviceConfig.sshPort, deviceConfig.sshUsername,
        deviceConfig.sshPrivateKey, deviceConfig.sshPublicKey,
        deviceConfig.sshKeyPassphrase);
    deviceConfig.sshPrivateKey = "";
    deviceConfig.sshPublicKey = "";
  } else {
    globalSSHConfig.setSSHServer(deviceConfig.sshHost, deviceConfig.sshPort,
                                 deviceConfig.sshUsername,
                                 deviceConfig.sshPassword);
    deviceConfig.sshPassword = "";
  }
  if (ENABLE_MULTI_TUNNEL_DEMO)
    configureMultiTunnelMappings();
  else
    globalSSHConfig.setTunnelConfig(deviceConfig.remoteBindHost,
        deviceConfig.remoteBindPort, deviceConfig.localHost,
        deviceConfig.localPort);
#if SSH_TUNNEL_LOW_MEMORY_PROFILE
  globalSSHConfig.setMaxReverseListeners(1);
#endif
  globalSSHConfig.setConnectionConfig(30, 5000, 5, 30);
  globalSSHConfig.setBufferConfig(TUNNEL_TRANSPORT_BUFFER_SIZE,
      TUNNEL_MAX_CHANNELS, 1800000, TUNNEL_RING_BUFFER_SIZE);
  globalSSHConfig.setDebugConfig(true, 115200);
  registerTunnelCallbacks();
  LOG_I("CONFIG", "Configuration complete");
}

void applyPendingMinisConfig() {
  minis_registration::ManagedTunnelConfig managed;
  if (!minis_registration::takeManagedTunnelConfig(managed))
    return;
  const String managedUsername = minis_registration::sid();
  const bool alreadyActive =
      deviceConfig.tunnelEnabled == managed.enabled &&
      deviceConfig.sshHost == managed.sshHost &&
      deviceConfig.sshPort == managed.sshPort &&
      deviceConfig.sshUsername == managedUsername &&
      deviceConfig.remoteBindHost == managed.remoteBindHost &&
      deviceConfig.remoteBindPort == managed.remoteBindPort &&
      deviceConfig.localHost == managed.localHost &&
      deviceConfig.localPort == managed.localPort;
  if (alreadyActive) {
    LOG_I("MINIS", "Managed tunnel config is already active");
    return;
  }
  const SSHServerConfig previousSsh = globalSSHConfig.getSSHConfig();
  const TunnelConfig previousTunnel = globalSSHConfig.getTunnelConfig();
  if (!previousSsh.useSSHKey || previousSsh.privateKeyData.isEmpty()) {
    LOG_W("MINIS", "Managed tunnel config rejected: a locally stored private key is required");
    return;
  }
  DeviceRuntimeConfig next = deviceConfig;
  next.tunnelEnabled = managed.enabled;
  next.sshHost = managed.sshHost;
  next.sshPort = managed.sshPort;
  next.sshUsername = managedUsername;
  next.remoteBindHost = managed.remoteBindHost;
  next.remoteBindPort = managed.remoteBindPort;
  next.localHost = managed.localHost;
  next.localPort = managed.localPort;
  LOGF_I("MINIS", "Applying managed tunnel config: enabled=%s ssh=%s@%s:%u remote=%s:%u local=%s:%u",
         next.tunnelEnabled ? "yes" : "no", next.sshUsername.c_str(),
         next.sshHost.c_str(), static_cast<unsigned int>(next.sshPort),
         next.remoteBindHost.c_str(),
         static_cast<unsigned int>(next.remoteBindPort), next.localHost.c_str(),
         static_cast<unsigned int>(next.localPort));
  tunnel.disconnect();
  globalSSHConfig.setSSHKeyAuthFromMemory(next.sshHost, next.sshPort,
      next.sshUsername, previousSsh.privateKeyData, previousSsh.publicKeyData,
      previousSsh.password);
  globalSSHConfig.setTunnelConfig(next.remoteBindHost, next.remoteBindPort,
      next.localHost, next.localPort);
  bool activated = true;
  if (next.tunnelEnabled) {
    status_led::set(status_led::State::Connecting);
    activated = tunnel.connectSSH();
  }
  if (activated && wifi_provisioning::saveManagedConfig(next)) {
    deviceConfig = next;
    if (next.tunnelEnabled)
      LOG_I("MINIS", "Managed tunnel config activated and stored");
    else {
      status_led::set(status_led::State::Disabled);
      LOG_I("MINIS", "Managed tunnel disabled and configuration stored");
    }
    return;
  }
  if (activated)
    LOG_E("MINIS", "Unable to store managed tunnel config; rolling back");
  else
    LOG_W("MINIS", "Managed tunnel activation failed; rolling back");
  tunnel.disconnect();
  globalSSHConfig.setSSHKeyAuthFromMemory(previousSsh.host, previousSsh.port,
      previousSsh.username, previousSsh.privateKeyData, previousSsh.publicKeyData,
      previousSsh.password);
  globalSSHConfig.setTunnelConfig(previousTunnel.remoteBindHost,
      previousTunnel.remoteBindPort, previousTunnel.localHost,
      previousTunnel.localPort);
  if (deviceConfig.tunnelEnabled) {
    status_led::set(status_led::State::Connecting);
    if (tunnel.connectSSH())
      LOG_I("MINIS", "Previous tunnel configuration restored");
    else {
      status_led::set(status_led::State::Error);
      LOG_E("MINIS", "Previous tunnel configuration could not reconnect");
    }
  } else
    status_led::set(status_led::State::Disabled);
}

void configureMultiTunnelMappings() {
  LOG_I("CONFIG", "Configuring multi-tunnel demo mappings");
  globalSSHConfig.clearTunnelMappings();
  globalSSHConfig.setMaxReverseListeners(3);
  globalSSHConfig.addTunnelMapping("127.0.0.1", 22080, "192.168.1.100", 80);
  globalSSHConfig.addTunnelMapping("127.0.0.1", 22081, "192.168.1.150", 502);
  globalSSHConfig.addTunnelMapping("127.0.0.1", 22082, "192.168.1.200", 22);
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
  if (now - lastStatsReport < STATS_INTERVAL)
    return;
  lastStatsReport = now;
  size_t freeHeap = ESP.getFreeHeap();
  size_t minFreeHeap = ESP.getMinFreeHeap();
  size_t largestFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (freeHeap > 10000) {
    LOGF_I("STATS", "Tunnel State: %s", tunnel.getStateString().c_str());
    LOGF_I("STATS", "Active Channels: %d", tunnel.getActiveChannels());
    LOGF_I("STATS", "Bytes Sent: %lu", tunnel.getBytesSent());
    LOGF_I("STATS", "Bytes Received: %lu", tunnel.getBytesReceived());
    LOGF_I("STATS", "Bytes Dropped: %lu", tunnel.getBytesDropped());
  }
  static unsigned long lastBytesSent = 0;
  static unsigned long lastBytesReceived = 0;
  unsigned long bytesSent = tunnel.getBytesSent();
  unsigned long bytesReceived = tunnel.getBytesReceived();
  unsigned long sentRate =
      (bytesSent - lastBytesSent) * 1000 / STATS_INTERVAL;
  unsigned long receivedRate =
      (bytesReceived - lastBytesReceived) * 1000 / STATS_INTERVAL;
  if (freeHeap > 8000) {
    LOGF_I("STATS", "Send Rate: %lu B/s", sentRate);
    LOGF_I("STATS", "Receive Rate: %lu B/s", receivedRate);
  }
  lastBytesSent = bytesSent;
  lastBytesReceived = bytesReceived;
  LOGF_I("WIFI", "RSSI: %d dBm", WiFi.RSSI());
  LOGF_I("SYSTEM", "Free Heap: %d bytes (min: %d, largest: %d)", freeHeap,
         minFreeHeap, largestFreeBlock);
  LOGF_I("SYSTEM", "Uptime: %lu seconds", millis() / 1000);
  if (freeHeap < CRITICAL_FREE_HEAP_BYTES)
    LOGF_W("MEMORY", "Critical free heap: %u bytes", (unsigned)freeHeap);
  if (freeHeap >= CRITICAL_FREE_HEAP_BYTES &&
      largestFreeBlock < CRITICAL_LARGEST_BLOCK_BYTES)
    LOGF_W("MEMORY", "Critical heap fragmentation: largest block %u bytes",
           (unsigned)largestFreeBlock);
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
void onSessionConnected() {
  status_led::set(status_led::State::Connected);
  LOG_I("CALLBACK", "SSH session established");
}
void onSessionDisconnected() {
  status_led::set(status_led::State::Error);
  LOG_I("CALLBACK", "SSH session disconnected");
}
void onChannelOpened(int channel) {
  LOGF_I("CALLBACK", "Channel %d opened", channel);
}
void onChannelClosed(int channel, ChannelCloseReason reason) {
  LOGF_I("CALLBACK", "Channel %d closed (%s)", channel,
         closeReasonToString(reason));
}
void onTunnelError(int code, const char *detail) {
  status_led::set(status_led::State::Error);
  LOGF_W("CALLBACK", "Tunnel error %d: %s", code,
         detail ? detail : "(none)");
}
