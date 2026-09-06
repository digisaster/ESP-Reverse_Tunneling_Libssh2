#include "minis_registration.h"

#include "ESP-Reverse_Tunneling_Libssh2.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>

namespace minis_registration {
namespace {

constexpr const char *MINIS_HOST = "cloud.supcom.nl";
constexpr uint16_t MINIS_HTTPS_PORT = 443;
constexpr const char *MINIS_PATH = "/hb/";
constexpr const char *MINIS_BASE_URL = "https://cloud.supcom.nl/hb/";
constexpr const char *MINIS_USER_AGENT = "MHB;vESP32";
constexpr uint32_t MINIS_TIMEOUT_MS = 5000;

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
  secureClient.setTimeout(MINIS_TIMEOUT_MS);

  if (!secureClient.connect(MINIS_HOST, MINIS_HTTPS_PORT, MINIS_TIMEOUT_MS)) {
    LOG_W("MINIS", "Bootstrap request failed: -1");
    return false;
  }

  secureClient.print(F("GET "));
  secureClient.print(MINIS_PATH);
  secureClient.print(clientSid);
  secureClient.print(F("/ HTTP/1.1\r\nHost: "));
  secureClient.print(MINIS_HOST);
  secureClient.print(F("\r\nUser-Agent: "));
  secureClient.print(MINIS_USER_AGENT);
  secureClient.print(F("\r\nConnection: close\r\n\r\n"));

  char statusLine[32] = {0};
  const size_t length =
      secureClient.readBytesUntil('\n', statusLine, sizeof(statusLine) - 1);
  secureClient.stop();

  const char *separator = nullptr;
  for (size_t i = 0; i < length; ++i) {
    if (statusLine[i] == ' ') {
      separator = &statusLine[i];
      break;
    }
  }

  if (separator == nullptr || separator + 3 >= statusLine + length ||
      separator[1] < '0' || separator[1] > '9' || separator[2] < '0' ||
      separator[2] > '9' || separator[3] < '0' || separator[3] > '9') {
    LOG_W("MINIS", "Bootstrap request failed: -1");
    return false;
  }

  const int status = (separator[1] - '0') * 100 + (separator[2] - '0') * 10 +
                     (separator[3] - '0');

  LOGF_I("MINIS", "Bootstrap HTTP status: %d", status);
  // For a brand-new client the expected first response is 404/403. The
  // existing Minis watcher uses that request to create the client directory.
  return true;
}

} // namespace minis_registration
