#include "minis_public_key_upload.h"

#include "minis_registration.h"
#include "ESP-Reverse_Tunneling_Libssh2.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>

namespace minis_public_key_upload {
namespace {
constexpr char HOST[] = "cloud.supcom.nl";
constexpr uint16_t PORT = 443;
constexpr char PATH[] = "/uploot.php";
constexpr char BOUNDARY[] = "----ESP32MinisPublicKey7MA4YWxkTrZu0gW";

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

  String key = publicKey;
  key.trim();
  key += "\n";

  const String sid = minis_registration::sid();
  String body;
  body.reserve(key.length() + 512);
  body += "--";
  body += BOUNDARY;
  body += "\r\nContent-Disposition: form-data; name=\"sid\"\r\n\r\n";
  body += sid;
  body += "\r\n--";
  body += BOUNDARY;
  body += "\r\nContent-Disposition: form-data; name=\"dest\"\r\n\r\nui";
  body += "\r\n--";
  body += BOUNDARY;
  body += "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"ssh_public_key.txt\"\r\n";
  body += "Content-Type: text/plain\r\n\r\n";
  body += key;
  body += "\r\n--";
  body += BOUNDARY;
  body += "--\r\n";

  WiFiClientSecure client;
  // Matches the current Minis POC transport. Do not use this channel for
  // private-key delivery; only the public key is uploaded here.
  client.setInsecure();
  client.setTimeout(10000);
  if (!client.connect(HOST, PORT)) {
    LOG_W("MINIS", "Public key upload failed: HTTPS connection failed");
    return false;
  }

  String headers;
  headers.reserve(320);
  headers += "POST ";
  headers += PATH;
  headers += " HTTP/1.1\r\nHost: ";
  headers += HOST;
  headers += "\r\n";
  headers += "User-Agent: MHB;ESP32;publickey\r\n";
  headers += "Content-Type: multipart/form-data; boundary=";
  headers += BOUNDARY;
  headers += "\r\n";
  headers += "Content-Length: ";
  headers += String(body.length());
  headers += "\r\nConnection: close\r\n\r\n";

  if (!writeAll(client, headers) || !writeAll(client, body)) {
    LOG_W("MINIS", "Public key upload failed while sending request");
    client.stop();
    return false;
  }

  const unsigned long deadline = millis() + 10000;
  while (!client.available() && client.connected() &&
         static_cast<long>(deadline - millis()) > 0) {
    delay(10);
  }
  if (!client.available()) {
    LOG_W("MINIS", "Public key upload failed: no HTTP response");
    client.stop();
    return false;
  }

  const String statusLine = client.readStringUntil('\n');
  const bool ok = statusLine.indexOf(" 200 ") >= 0;
  if (ok) {
    LOGF_I("MINIS", "Public key uploaded for SID %s -> ui/ssh_public_key.txt",
           sid.c_str());
  } else {
    LOGF_W("MINIS", "Public key upload HTTP failure: %s",
           statusLine.c_str());
  }
  client.stop();
  return ok;
}

} // namespace minis_public_key_upload
