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

// Starts the Minis background task. It loads the cached heartbeat interval,
// checks cfg.txt once shortly after startup, sends jittered heartbeats, and
// checks cfg.txt again after each successful heartbeat. TLS work is skipped
// when the C3 does not have enough free/contiguous heap.
bool startHeartbeatTask();

// Returns the latest complete tunnel config received from Minis. The main task
// persists changed settings and restarts the device; the background task never
// manipulates the SSH session directly.
bool takeManagedTunnelConfig(ManagedTunnelConfig &config);

} // namespace minis_registration
