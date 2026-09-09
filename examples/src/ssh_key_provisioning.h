#pragma once

#include "wifi_provisioning.h"

namespace ssh_key_provisioning {
// Ensures a locally stored ECDSA P-256 key pair exists and loads it into the
// runtime configuration. Existing complete SSH configurations are not changed.
// The private key remains on the ESP32; callers may upload only publicKey.
bool ensure(DeviceRuntimeConfig &config);
} // namespace ssh_key_provisioning
