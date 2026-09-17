#pragma once

#include <Arduino.h>

enum class MacVendor { Original, Cisco, HP };

namespace mac_vendor {

const char *configValue(MacVendor vendor);
bool parse(const String &value, MacVendor &vendor);

// Prepare the WiFi STA MAC before any WiFi interface is initialized. The final
// three bytes stay equal to the factory ESP32 MAC, so only the vendor prefix
// changes. Original leaves the factory address untouched.
bool prepareStation(MacVendor vendor);

// Factory-programmed Espressif base/station MAC, unaffected by overrides.
String hardwareAddress();
// Current WiFi STA MAC as exposed on the network after WiFi initialization.
String activeAddress();
MacVendor appliedVendor();

} // namespace mac_vendor
