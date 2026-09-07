#pragma once

#include <Arduino.h>

namespace minis_registration {

struct ManagedTunnelConfig {
  bool enabled = false;
  String sshHost;
  uint16_t sshPort = 0;
  String remoteBindHost;
  uint16_t remoteBindPort = 0;
  String localHost;
  uint16_t localPort = 0;
};

// Returns a stable Minis-compatible SID consisting of 8 lowercase hex chars.
String sid();

// Performs the first Minis bootstrap request. The existing Minis watcher
// creates /hb/<sid>/ when it sees the first MHB request for an unknown SID.
bool registerClient();

// Starts the non-blocking Minis service task. It reads cfg.txt, applies the
// bounded HB_INTERVAL_MIN, and sends heartbeats with fresh 0..interval jitter.
// A complete SSH/reverse-tunnel config is validated and stored as /minis.cfg.
// Runtime activation is handed to the main task to avoid touching the SSH
// session from this background task.
bool startHeartbeatTask();

// Returns the latest complete config received from Minis, if one is waiting.
bool takeManagedTunnelConfig(ManagedTunnelConfig &config);

} // namespace minis_registration
