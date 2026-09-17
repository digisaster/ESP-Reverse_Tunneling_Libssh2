#pragma once

#include <Arduino.h>

enum class MacVendor { Original, Cisco, HP };

namespace mac_vendor {

const char *configValue(MacVendor vendor);
bool parse(const String &value, MacVendor &vendor);

// Apply the selected vendor OUI to the WiFi STA interface. The final three
// bytes stay equal to the device's original station MAC, so only the vendor
// prefix changes. Call after WiFi.mode(WIFI_STA) and before WiFi.begin().
bool prepareStation(MacVendor vendor);

// Original station MAC captured before any vendor override is applied.
String hardwareAddress();
// Current WiFi STA MAC as exposed on the network.
String activeAddress();
MacVendor appliedVendor();

} // namespace mac_vendor
