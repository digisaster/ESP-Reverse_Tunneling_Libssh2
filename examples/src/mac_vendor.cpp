#include "mac_vendor.h"

#include "logger.h"
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_wifi.h>

namespace mac_vendor {
namespace {
// MA-L OUIs registered to the named vendors. Only the first three octets are
// replaced; the device-specific last three octets stay tied to this ESP32.
constexpr uint8_t CISCO_OUI[3] = {0x00, 0x00, 0x0C};
constexpr uint8_t HP_OUI[3] = {0x3C, 0xD9, 0x2B};
MacVendor currentVendor = MacVendor::Original;

bool originalStationMac(uint8_t mac[6]) {
  return esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK;
}

String formatMac(const uint8_t mac[6]) {
  char text[18] = {0};
  snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],
           mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(text);
}
} // namespace

const char *configValue(MacVendor vendor) {
  switch (vendor) {
  case MacVendor::Cisco:
    return "CISCO";
  case MacVendor::HP:
    return "HP";
  default:
    return "ORIGINAL";
  }
}

bool parse(const String &value, MacVendor &vendor) {
  String normalized = value;
  normalized.trim();
  normalized.toUpperCase();
  if (normalized == "ORIGINAL" || normalized == "ESP32") {
    vendor = MacVendor::Original;
    return true;
  }
  if (normalized == "CISCO") {
    vendor = MacVendor::Cisco;
    return true;
  }
  if (normalized == "HP" || normalized == "HEWLETT-PACKARD" ||
      normalized == "HEWLETT_PACKARD") {
    vendor = MacVendor::HP;
    return true;
  }
  return false;
}

bool prepareStation(MacVendor vendor) {
  uint8_t mac[6] = {0};
  if (!originalStationMac(mac)) {
    LOG_E("WIFI", "Unable to read original ESP32 station MAC");
    return false;
  }

  const uint8_t *oui = nullptr;
  if (vendor == MacVendor::Cisco)
    oui = CISCO_OUI;
  else if (vendor == MacVendor::HP)
    oui = HP_OUI;

  if (oui != nullptr) {
    mac[0] = oui[0];
    mac[1] = oui[1];
    mac[2] = oui[2];
  }

  const esp_err_t result = esp_wifi_set_mac(WIFI_IF_STA, mac);
  if (result != ESP_OK) {
    LOGF_E("WIFI", "Unable to apply %s MAC profile (esp_err=%d)",
           configValue(vendor), static_cast<int>(result));
    return false;
  }

  currentVendor = vendor;
  LOGF_I("WIFI", "MAC profile %s active: %s", configValue(vendor),
         formatMac(mac).c_str());
  return true;
}

String hardwareAddress() {
  uint8_t mac[6] = {0};
  return originalStationMac(mac) ? formatMac(mac) : String("unavailable");
}

String activeAddress() { return WiFi.macAddress(); }

MacVendor appliedVendor() { return currentVendor; }

} // namespace mac_vendor
