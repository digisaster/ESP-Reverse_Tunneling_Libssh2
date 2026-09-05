#pragma once

#include <Arduino.h>

namespace minis_registration {

// Returns a stable Minis-compatible SID consisting of 8 uppercase hex chars.
String sid();

// Performs the first Minis bootstrap request. The existing Minis watcher
// creates /hb/<sid>/ when it sees the first MHB request for an unknown SID.
bool registerClient();

} // namespace minis_registration
