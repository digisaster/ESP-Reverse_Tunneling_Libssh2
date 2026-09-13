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

// TLS on the C3 can temporarily run out of contiguous heap while libssh2 is
// active. The Minis background task never manipulates the SSH tunnel directly;
// instead it requests a short maintenance window from the main task. The main
// task may confirm the pause when the tunnel is idle, or defer the request when
// an active forwarded channel must not be interrupted.
bool tunnelPauseRequestedForControlPlane();
void confirmTunnelPausedForControlPlane();
void deferTunnelPauseForControlPlane();
bool takeTunnelResumeRequest();

} // namespace minis_registration