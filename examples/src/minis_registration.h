#pragma once

#include <Arduino.h>

namespace minis_registration {

constexpr char FIRMWARE_VERSION[] = "1.0.0-beta.1";

struct ManagedTunnelConfig {
  bool enabled = false;
  String sshHost;
  uint16_t sshPort = 0;
  String remoteBindHost;
  uint16_t remoteBindPort = 0;
  String localHost;
  uint16_t localPort = 0;
  bool macVendorPresent = false;
  String macVendor;
};

// Returns a stable Minis-compatible SID consisting of 8 lowercase hex chars.
String sid();

// Performs the first Minis bootstrap request. The existing Minis watcher
// creates /hb/<sid>/ when it sees the first MHB request for an unknown SID.
bool registerClient();

// Starts the Minis background task. It loads the cached heartbeat interval
// and fetches cfg.txt once per heartbeat cycle. That single HTTPS GET is both
// the heartbeat and the configuration check. TLS work is skipped when the C3
// does not have enough free/contiguous heap.
bool startHeartbeatTask();

// Queues a small alert for delivery to the existing Minis alert endpoint.
// This call never opens a network connection. Runtime HTTPS delivery is owned
// exclusively by the Minis background task and obeys the same TLS memory guard
// as heartbeat/config traffic. Returns false when the service is not running,
// the arguments are invalid, or the bounded queue is full.
bool queueAlert(const char *level, const char *title, const char *message,
                const char *tag = nullptr);

// Returns the latest complete tunnel config received from Minis. MAC_VENDOR is
// optional; absent means keep the locally selected WiFi MAC profile.
bool takeManagedTunnelConfig(ManagedTunnelConfig &config);

} // namespace minis_registration
