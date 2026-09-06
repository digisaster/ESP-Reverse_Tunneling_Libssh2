#pragma once

#include <Arduino.h>

namespace minis_registration {

// Returns a stable Minis-compatible SID consisting of 8 lowercase hex chars.
String sid();

// Performs the first Minis bootstrap request. The existing Minis watcher
// creates /hb/<sid>/ when it sees the first MHB request for an unknown SID.
bool registerClient();

// Starts the non-blocking Minis service task. It reads cfg.txt, applies the
// bounded HB_INTERVAL_MIN, and sends heartbeats with fresh 0..interval jitter.
// A complete SSH/reverse-tunnel candidate is validated, stored as /minis.cfg,
// and restored after reboot. It does not alter or restart the active tunnel.
bool startHeartbeatTask();

} // namespace minis_registration
