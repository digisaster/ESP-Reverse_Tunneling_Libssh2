#pragma once

#include <Arduino.h>

namespace minis_registration {

// Returns a stable Minis-compatible SID consisting of 8 uppercase hex chars.
String sid();

// Performs the first Minis bootstrap request. The existing Minis watcher
// creates /hb/<sid>/ when it sees the first MHB request for an unknown SID.
bool registerClient();

// Starts the non-blocking Minis service task. It reads cfg.txt and then sends
// heartbeats using HB_INTERVAL_MIN plus fresh jitter of 0..HB_INTERVAL_MIN.
// Failure is non-fatal and leaves the default 15-minute interval active.
bool startHeartbeatTask();

} // namespace minis_registration
