#include "minis_registration.h"

#include "ESP-Reverse_Tunneling_Libssh2.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <climits>
#include <cstring>

namespace minis_registration {
namespace {

constexpr const char *MINIS_HOST = "cloud.supcom.nl";
constexpr uint16_t MINIS_HTTPS_PORT = 443;
constexpr const char *MINIS_PATH = "/hb/";
constexpr const char *MINIS_BASE_URL = "https://cloud.supcom.nl/hb/";
constexpr const char *MINIS_USER_AGENT = "MHB;vESP32";
constexpr uint32_t MINIS_TIMEOUT_MS = 5000;
constexpr uint16_t DEFAULT_HEARTBEAT_INTERVAL_MIN = 15;
constexpr uint16_t MIN_HEARTBEAT_INTERVAL_MIN = 5;
constexpr uint16_t MAX_HEARTBEAT_INTERVAL_MIN = 1440;
constexpr uint32_t INITIAL_CONFIG_DELAY_MS = 5000;
constexpr uint32_t HEARTBEAT_TASK_STACK_BYTES = 6144;
constexpr size_t CONFIG_BUFFER_SIZE = 384;

TaskHandle_t heartbeatTaskHandle = nullptr;
uint16_t heartbeatIntervalMin = DEFAULT_HEARTBEAT_INTERVAL_MIN;

String buildSid() {
  const uint64_t chipId = ESP.getEfuseMac();
  const uint32_t shortId = static_cast<uint32_t>(chipId & 0xFFFFFFFFULL);

  char buffer[9] = {0};
  snprintf(buffer, sizeof(buffer), "%08X", static_cast<unsigned int>(shortId));
  return String(buffer);
}

int parseHttpStatus(const char *line, size_t length) {
  const char *separator = nullptr;
  for (size_t i = 0; i < length; ++i) {
    if (line[i] == ' ') {
      separator = &line[i];
      break;
    }
  }

  if (separator == nullptr || separator + 3 >= line + length ||
      separator[1] < '0' || separator[1] > '9' || separator[2] < '0' ||
      separator[2] > '9' || separator[3] < '0' || separator[3] > '9') {
    return -1;
  }

  return (separator[1] - '0') * 100 + (separator[2] - '0') * 10 +
         (separator[3] - '0');
}

bool parseUnsigned(const char *text, uint32_t &value) {
  if (*text < '0' || *text > '9') {
    return false;
  }

  uint32_t parsed = 0;
  while (*text >= '0' && *text <= '9') {
    const uint32_t digit = static_cast<uint32_t>(*text - '0');
    if (parsed > (UINT32_MAX - digit) / 10U) {
      return false;
    }
    parsed = parsed * 10U + digit;
    ++text;
  }

  while (*text == ' ' || *text == '\t' || *text == '\r') {
    ++text;
  }
  if (*text != '\0' && *text != '\n' && *text != '#') {
    return false;
  }

  value = parsed;
  return true;
}

int performGet(const char *suffix, char *response, size_t responseCapacity,
               size_t *responseLength) {
  if (responseLength != nullptr) {
    *responseLength = 0;
  }

  WiFiClientSecure secureClient;
  // Certificate validation will be enabled before remote tunnel settings are
  // accepted. This step only activates a safely bounded heartbeat interval.
  secureClient.setInsecure();
  secureClient.setTimeout(MINIS_TIMEOUT_MS);

  if (!secureClient.connect(MINIS_HOST, MINIS_HTTPS_PORT, MINIS_TIMEOUT_MS)) {
    return -1;
  }

  secureClient.print(F("GET "));
  secureClient.print(MINIS_PATH);
  secureClient.print(sid());
  secureClient.print(suffix);
  secureClient.print(F(" HTTP/1.1\r\nHost: "));
  secureClient.print(MINIS_HOST);
  secureClient.print(F("\r\nUser-Agent: "));
  secureClient.print(MINIS_USER_AGENT);
  secureClient.print(F("\r\nConnection: close\r\n\r\n"));

  char line[96] = {0};
  const size_t statusLength =
      secureClient.readBytesUntil('\n', line, sizeof(line) - 1);
  const int status = parseHttpStatus(line, statusLength);
  if (status < 0 || response == nullptr || responseCapacity < 2 ||
      status != 200) {
    secureClient.stop();
    return status;
  }

  int contentLength = -1;
  while (true) {
    memset(line, 0, sizeof(line));
    const size_t length =
        secureClient.readBytesUntil('\n', line, sizeof(line) - 1);
    if (length == 0 || (length == 1 && line[0] == '\r')) {
      break;
    }

    constexpr char CONTENT_LENGTH_HEADER[] = "Content-Length:";
    if (strncmp(line, CONTENT_LENGTH_HEADER,
                sizeof(CONTENT_LENGTH_HEADER) - 1) == 0) {
      const char *value = line + sizeof(CONTENT_LENGTH_HEADER) - 1;
      while (*value == ' ' || *value == '\t') {
        ++value;
      }
      uint32_t parsedLength = 0;
      if (parseUnsigned(value, parsedLength) && parsedLength <= INT_MAX) {
        contentLength = static_cast<int>(parsedLength);
      }
    }
  }

  size_t used = 0;
  bool complete = true;
  if (contentLength >= 0) {
    if (static_cast<size_t>(contentLength) >= responseCapacity) {
      complete = false;
    } else {
      used = secureClient.readBytes(response, contentLength);
      complete = used == static_cast<size_t>(contentLength);
    }
  } else {
    uint32_t lastDataAt = millis();
    while (secureClient.connected() || secureClient.available()) {
      while (secureClient.available()) {
        const int value = secureClient.read();
        if (value < 0) {
          break;
        }
        lastDataAt = millis();
        if (used + 1 < responseCapacity) {
          response[used++] = static_cast<char>(value);
        } else {
          complete = false;
        }
      }
      if (millis() - lastDataAt >= MINIS_TIMEOUT_MS) {
        complete = false;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }

  secureClient.stop();
  if (!complete) {
    return -1;
  }

  response[used] = '\0';
  if (responseLength != nullptr) {
    *responseLength = used;
  }
  return status;
}

bool sendHeartbeat(bool bootstrap) {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_W("MINIS", "Heartbeat skipped: WiFi not connected");
    return false;
  }

  const String clientSid = sid();
  const String url = String(MINIS_BASE_URL) + clientSid + "/";
  LOGF_I("MINIS", "%s GET: %s", bootstrap ? "Bootstrap" : "Heartbeat",
         url.c_str());

  const int status = performGet("/", nullptr, 0, nullptr);
  if (status > 0) {
    LOGF_I("MINIS", "%s HTTP status: %d",
           bootstrap ? "Bootstrap" : "Heartbeat", status);
    return true;
  }

  LOGF_W("MINIS", "%s request failed: %d",
         bootstrap ? "Bootstrap" : "Heartbeat", status);
  return false;
}

bool parseHeartbeatInterval(char *config, uint16_t &intervalMin) {
  constexpr char KEY[] = "HB_INTERVAL_MIN";
  char *line = config;

  while (*line != '\0') {
    char *nextLine = strchr(line, '\n');
    if (nextLine != nullptr) {
      *nextLine = '\0';
    }

    while (*line == ' ' || *line == '\t' || *line == '\r') {
      ++line;
    }
    if (*line != '#' && strncmp(line, KEY, sizeof(KEY) - 1) == 0) {
      char *value = line + sizeof(KEY) - 1;
      while (*value == ' ' || *value == '\t') {
        ++value;
      }
      if (*value == '=') {
        ++value;
        while (*value == ' ' || *value == '\t') {
          ++value;
        }

        uint32_t parsed = 0;
        if (parseUnsigned(value, parsed) &&
            parsed >= MIN_HEARTBEAT_INTERVAL_MIN &&
            parsed <= MAX_HEARTBEAT_INTERVAL_MIN) {
          intervalMin = static_cast<uint16_t>(parsed);
          return true;
        }
        return false;
      }
    }

    if (nextLine == nullptr) {
      break;
    }
    line = nextLine + 1;
  }
  return false;
}

void refreshConfig() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_W("MINIS", "Config check skipped: WiFi not connected");
    return;
  }

  char config[CONFIG_BUFFER_SIZE] = {0};
  size_t configLength = 0;
  const int status =
      performGet("/cfg.txt", config, sizeof(config), &configLength);
  if (status != 200) {
    LOGF_I("MINIS", "cfg.txt unavailable (HTTP %d); keeping %u min", status,
           static_cast<unsigned int>(heartbeatIntervalMin));
    return;
  }

  uint16_t configuredInterval = 0;
  if (configLength == 0 ||
      !parseHeartbeatInterval(config, configuredInterval)) {
    LOGF_W("MINIS", "cfg.txt has no valid HB_INTERVAL_MIN; keeping %u min",
           static_cast<unsigned int>(heartbeatIntervalMin));
    return;
  }

  if (configuredInterval != heartbeatIntervalMin) {
    LOGF_I("MINIS", "HB_INTERVAL_MIN changed: %u -> %u",
           static_cast<unsigned int>(heartbeatIntervalMin),
           static_cast<unsigned int>(configuredInterval));
    heartbeatIntervalMin = configuredInterval;
  } else {
    LOGF_I("MINIS", "HB_INTERVAL_MIN: %u",
           static_cast<unsigned int>(heartbeatIntervalMin));
  }
}

uint32_t nextHeartbeatDelaySeconds() {
  const uint32_t baseSeconds =
      static_cast<uint32_t>(heartbeatIntervalMin) * 60U;
  const uint32_t jitterSeconds = esp_random() % (baseSeconds + 1U);
  return baseSeconds + jitterSeconds;
}

void heartbeatTask(void *) {
  vTaskDelay(pdMS_TO_TICKS(INITIAL_CONFIG_DELAY_MS));
  refreshConfig();

  while (true) {
    const uint32_t delaySeconds = nextHeartbeatDelaySeconds();
    LOGF_I("MINIS", "Next heartbeat/config check in %lu min %lu sec",
           static_cast<unsigned long>(delaySeconds / 60U),
           static_cast<unsigned long>(delaySeconds % 60U));
    vTaskDelay(pdMS_TO_TICKS(static_cast<uint64_t>(delaySeconds) * 1000ULL));

    sendHeartbeat(false);
    refreshConfig();
  }
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

  LOGF_I("MINIS", "SID: %s", clientSid.c_str());
  return sendHeartbeat(true);
}

bool startHeartbeatTask() {
  if (heartbeatTaskHandle != nullptr) {
    return true;
  }

  if (xTaskCreate(heartbeatTask, "minis_hb", HEARTBEAT_TASK_STACK_BYTES,
                  nullptr, 1, &heartbeatTaskHandle) != pdPASS) {
    heartbeatTaskHandle = nullptr;
    LOG_W("MINIS", "Unable to start heartbeat task");
    return false;
  }
  return true;
}

} // namespace minis_registration
