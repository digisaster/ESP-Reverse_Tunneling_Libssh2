#include "minis_public_key_upload.h"

#include "minis_registration.h"
#include "ESP-Reverse_Tunneling_Libssh2.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

namespace minis_public_key_upload {
namespace {
constexpr char HOST[] = "cloud.supcom.nl";
constexpr uint16_t PORT = 443;
constexpr char PATH[] = "/uploot.php";
constexpr char BOUNDARY[] = "----ESP32MinisUpload7MA4YWxkTrZu0gW";
constexpr char GENESIS_MARKER_PATH[] = "/esp32tun.genesis_key";

bool writeAll(WiFiClientSecure &client, const String &data) {
  const uint8_t *ptr = reinterpret_cast<const uint8_t *>(data.c_str());
  size_t remaining = data.length();
  while (remaining > 0) {
    const size_t written = client.write(ptr, remaining);
    if (written == 0)
      return false;
    ptr += written;
    remaining -= written;
  }
  return true;
}

String publicKeyDiagnosticTag(const String &key) {
  uint64_t hash = 1469598103934665603ULL;
  for (size_t i = 0; i < key.length(); ++i) {
    hash ^= static_cast<uint8_t>(key.charAt(i));
    hash *= 1099511628211ULL;
  }
  char text[17] = {0};
  const unsigned long hi = static_cast<unsigned long>(hash >> 32);
  const unsigned long lo = static_cast<unsigned long>(hash & 0xffffffffULL);
  snprintf(text, sizeof(text), "%08lx%08lx", hi, lo);
  return String(text);
}

String publicKeyAlgorithm(const String &publicKey) {
  String key = publicKey;
  key.trim();
  const int separator = key.indexOf(' ');
  if (separator <= 0)
    return String("unknown");
  return key.substring(0, separator);
}

String localTimestamp() {
  const time_t now = time(nullptr);
  if (now < 1700000000)
    return String("unavailable (use Minis Uploads/Timeline timestamp)");

  struct tm localTime {};
  localtime_r(&now, &localTime);
  char text[40] = {0};
  if (strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S %Z", &localTime) == 0)
    return String("unavailable (use Minis Uploads/Timeline timestamp)");
  return String(text);
}

bool uploadTextFile(const String &sid, const char *dest, const char *filename,
                    const String &payload, const char *userAgent,
                    const char *label) {
  String body;
  body.reserve(payload.length() + 640);
  body += "--";
  body += BOUNDARY;
  body += "\r\nContent-Disposition: form-data; name=\"sid\"\r\n\r\n";
  body += sid;

  if (dest != nullptr && dest[0] != '\0') {
    body += "\r\n--";
    body += BOUNDARY;
    body += "\r\nContent-Disposition: form-data; name=\"dest\"\r\n\r\n";
    body += dest;
  }

  body += "\r\n--";
  body += BOUNDARY;
  body += "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"";
  body += filename;
  body += "\"\r\nContent-Type: text/plain\r\n\r\n";
  body += payload;
  body += "\r\n--";
  body += BOUNDARY;
  body += "--\r\n";

  WiFiClientSecure client;
  // Matches the current Minis POC transport. No private key is ever sent by
  // this module; uploads contain only public/inventory information.
  client.setInsecure();
  client.setTimeout(10000);
  if (!client.connect(HOST, PORT)) {
    LOGF_W("MINIS", "%s upload failed: HTTPS connection failed", label);
    return false;
  }

  String headers;
  headers.reserve(360);
  headers += "POST ";
  headers += PATH;
  headers += " HTTP/1.1\r\nHost: ";
  headers += HOST;
  headers += "\r\nUser-Agent: ";
  headers += userAgent;
  headers += "\r\nContent-Type: multipart/form-data; boundary=";
  headers += BOUNDARY;
  headers += "\r\nContent-Length: ";
  headers += String(body.length());
  headers += "\r\nConnection: close\r\n\r\n";

  if (!writeAll(client, headers) || !writeAll(client, body)) {
    LOGF_W("MINIS", "%s upload failed while sending request", label);
    client.stop();
    return false;
  }

  const unsigned long deadline = millis() + 10000;
  while (!client.available() && client.connected() &&
         static_cast<long>(deadline - millis()) > 0) {
    delay(10);
  }
  if (!client.available()) {
    LOGF_W("MINIS", "%s upload failed: no HTTP response", label);
    client.stop();
    return false;
  }

  const String statusLine = client.readStringUntil('\n');
  const bool ok = statusLine.indexOf(" 200 ") >= 0;
  if (!ok)
    LOGF_W("MINIS", "%s upload HTTP failure: %s", label,
           statusLine.c_str());
  client.stop();
  return ok;
}

bool genesisAlreadySentForKey(const String &keyTag) {
  File marker = LittleFS.open(GENESIS_MARKER_PATH, "r");
  if (!marker)
    return false;
  String storedTag = marker.readString();
  marker.close();
  storedTag.trim();
  return storedTag == keyTag;
}

bool storeGenesisMarker(const String &keyTag) {
  File marker = LittleFS.open(GENESIS_MARKER_PATH, "w");
  if (!marker)
    return false;
  const size_t written = marker.print(keyTag);
  marker.flush();
  marker.close();
  return written == keyTag.length();
}

String buildGenesisReport(const String &sid, const String &publicKey,
                          const String &keyTag) {
  String report;
  report.reserve(768);
  report += "==== MHB GENESIS REPORT ====\n";
  report += "Client-Type: ESP32\n";
  report += "SID: ";
  report += sid;
  report += "\nFirst-Connect: ";
  report += localTimestamp();
  report += "\nFirst-Connect-Uptime-ms: ";
  report += String(millis());
  report += "\nChip: ";
  report += ESP.getChipModel();
  report += " rev ";
  report += String(ESP.getChipRevision());
  report += "\nFirmware-Build: ";
  report += __DATE__;
  report += " ";
  report += __TIME__;
  report += "\nMAC: ";
  report += WiFi.macAddress();
  report += "\nSSH-Key-Algorithm: ";
  report += publicKeyAlgorithm(publicKey);
  report += "\nSSH-Key-Tag: ";
  report += keyTag;
  report += "\n\n---- network ----\nIP: ";
  report += WiFi.localIP().toString();
  report += "\nNetmask: ";
  report += WiFi.subnetMask().toString();
  report += "\nGateway: ";
  report += WiFi.gatewayIP().toString();
  report += "\nDNS1: ";
  report += WiFi.dnsIP(0).toString();
  report += "\n";
  return report;
}

void uploadGenesisIfNeeded(const String &sid, const String &publicKey) {
  const String keyTag = publicKeyDiagnosticTag(publicKey);
  if (genesisAlreadySentForKey(keyTag)) {
    LOGF_I("MINIS", "Genesis already recorded for SSH key tag %s",
           keyTag.c_str());
    return;
  }

  const String report = buildGenesisReport(sid, publicKey, keyTag);
  const String userAgent = String("MHB;payload;genesis;ESP32;") + sid +
                           ";embedded";
  // No dest field on purpose: this follows the Windows genesis module and
  // therefore lands in the default per-client upload directory.
  if (!uploadTextFile(sid, nullptr, "genesis.txt", report,
                      userAgent.c_str(), "Genesis")) {
    LOG_W("MINIS", "Genesis upload did not complete; it will retry after a future reboot");
    return;
  }

  LOGF_I("MINIS", "Genesis uploaded for SID %s (SSH key tag %s)", sid.c_str(),
         keyTag.c_str());
  if (!storeGenesisMarker(keyTag)) {
    LOG_W("MINIS", "Genesis upload succeeded but local marker could not be stored; a later boot may upload it again");
  }
}
} // namespace

bool upload(const String &publicKey) {
  if (publicKey.isEmpty()) {
    LOG_W("MINIS", "Public key upload skipped: no public key configured");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    LOG_W("MINIS", "Public key upload skipped: WiFi unavailable");
    return false;
  }

  const String rawPublicKey = publicKey;
  String key = publicKey;
  key.trim();
  key += "\n";

  const String sid = minis_registration::sid();
  const bool publicKeyUploaded =
      uploadTextFile(sid, "ui", "ssh_public_key.txt", key,
                     "MHB;ESP32;publickey", "Public key");
  if (!publicKeyUploaded)
    return false;

  LOGF_I("MINIS", "Public key uploaded for SID %s -> ui/ssh_public_key.txt",
         sid.c_str());

  // Keep genesis outside the SSH runtime: this function is called during
  // startup before tunnel.connectSSH(). The key-tag marker makes it one-time
  // per SSH identity, so WiFi changes do not create duplicate genesis files.
  uploadGenesisIfNeeded(sid, rawPublicKey);
  return true;
}

} // namespace minis_public_key_upload
