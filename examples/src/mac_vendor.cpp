#include "mac_vendor.h"

#include "logger.h"
#include <WiFi.h>
#include <esp_mac.h>

namespace mac_vendor {
namespace {
// MA-L OUIs registered to the named vendors. Only the first three octets are
// replaced; the device-specific last three octets stay tied to this ESP32.
constexpr uint8_t CISCO_OUI[3] = {0x00, 0x00, 0x0C};
constexpr uint8_t HP_OUI[3] = {0x3C, 0xD9, 0x2B};
MacVendor currentVendor = MacVendor::Original;
bool stationPrepared = false;

bool factoryStationMac(uint8_t mac[6]) {
  // On ESP32-C3 the WiFi STA address is the factory base MAC. Reading EFUSE
  // directly keeps this value independent from any runtime interface override.
  return esp_efuse_mac_get_default(mac) == ESP_OK;
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
  // The normal reconnect path calls this again after WiFi.mode(WIFI_STA).
  // Never touch MAC configuration again once it was prepared pre-init.
  if (stationPrepared)
    return vendor == currentVendor;

  uint8_t mac[6] = {0};
  if (!factoryStationMac(mac)) {
    LOG_E("WIFI", "Unable to read factory ESP32 station MAC");
    return false;
  }

  if (vendor != MacVendor::Original) {
    const uint8_t *oui = vendor == MacVendor::Cisco ? CISCO_OUI : HP_OUI;
    mac[0] = oui[0];
    mac[1] = oui[1];
    mac[2] = oui[2];

    // This API may set an interface address before that interface exists.
    const esp_err_t result = esp_iface_mac_addr_set(mac, ESP_MAC_WIFI_STA);
    if (result != ESP_OK) {
      LOGF_E("WIFI", "Unable to prepare %s MAC profile (esp_err=%d)",
             configValue(vendor), static_cast<int>(result));
      return false;
    }
  }

  currentVendor = vendor;
  stationPrepared = true;
  LOGF_I("WIFI", "MAC profile %s prepared: %s", configValue(vendor),
         formatMac(mac).c_str());
  return true;
}

String hardwareAddress() {
  uint8_t mac[6] = {0};
  return factoryStationMac(mac) ? formatMac(mac) : String("unavailable");
}

String activeAddress() { return WiFi.macAddress(); }

MacVendor appliedVendor() { return currentVendor; }

} // namespace mac_vendor
