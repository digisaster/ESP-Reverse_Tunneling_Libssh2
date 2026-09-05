#include "minis_registration.h"

#include "ESP-Reverse_Tunneling_Libssh2.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

namespace minis_registration {
namespace {

constexpr const char *MINIS_BASE_URL = "https://cloud.supcom.nl/hb/";
constexpr const char *MINIS_USER_AGENT = "MHB;vESP32";

String buildSid() {
  const uint64_t chipId = ESP.getEfuseMac();
  const uint32_t shortId = static_cast<uint32_t>(chipId & 0xFFFFFFFFULL);

  char buffer[9] = {0};
  snprintf(buffer, sizeof(buffer), "%08X", static_cast<unsigned int>(shortId));
  return String(buffer);
}

} // namespace

String sid() {
  static const String cachedSid = buildSid();
  return cachedSid;
}

bool registerClient() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_W("MINIS", "Registration skipped: WiFi not connected");
    return false;
  }

  const String clientSid = sid();
  const String url = String(MINIS_BASE_URL) + clientSid + "/";

  LOGF_I("MINIS", "SID: %s", clientSid.c_str());
  LOGF_I("MINIS", "Bootstrap GET: %s", url.c_str());

  WiFiClientSecure secureClient;
  // Step 1 intentionally keeps this minimal. Certificate validation can be
  // hardened in a later step by pinning the server CA certificate.
  secureClient.setInsecure();

  HTTPClient http;
  if (!http.begin(secureClient, url)) {
    LOG_E("MINIS", "Unable to initialize HTTPS request");
    return false;
  }

  http.setUserAgent(MINIS_USER_AGENT);
  http.setConnectTimeout(5000);
  http.setTimeout(5000);

  const int status = http.GET();
  http.end();

  if (status > 0) {
    LOGF_I("MINIS", "Bootstrap HTTP status: %d", status);
    // For a brand-new client the expected first response is 404/403. The
    // existing Minis watcher uses that request to create the client directory.
    return true;
  }

  LOGF_W("MINIS", "Bootstrap request failed: %d", status);
  return false;
}

} // namespace minis_registration
