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
constexpr size_t CONFIG_BUFFER_SIZE = 768;
constexpr size_t MAX_CONFIG_HOST_LENGTH = 253;

TaskHandle_t heartbeatTaskHandle = nullptr;
uint16_t heartbeatIntervalMin = DEFAULT_HEARTBEAT_INTERVAL_MIN;
bool candidateTunnelKnown = false;
bool candidateTunnelEnabled = false;
String candidateRemoteBindHost;
uint16_t candidateRemoteBindPort = 0;
String candidateLocalHost;
uint16_t candidateLocalPort = 0;

enum class SettingResult { NotFound, Valid, Invalid };

struct ParsedConfig {
  bool heartbeatIntervalPresent = false;
  bool heartbeatIntervalValid = false;
  uint16_t heartbeatIntervalMin = 0;
  bool tunnelEnabledPresent = false;
  bool tunnelEnabledValid = false;
  bool tunnelEnabled = false;
  bool remoteBindHostPresent = false;
  bool remoteBindHostValid = false;
  const char *remoteBindHost = nullptr;
  bool remoteBindPortPresent = false;
  bool remoteBindPortValid = false;
  uint16_t remoteBindPort = 0;
  bool localHostPresent = false;
  bool localHostValid = false;
  const char *localHost = nullptr;
  bool localPortPresent = false;
  bool localPortValid = false;
  uint16_t localPort = 0;
};

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

SettingResult parseSetting(const char *line, const char *key, uint32_t &value) {
  const size_t keyLength = strlen(key);
  if (strncmp(line, key, keyLength) != 0) {
    return SettingResult::NotFound;
  }

  const char *settingValue = line + keyLength;
  while (*settingValue == ' ' || *settingValue == '\t') {
    ++settingValue;
  }
  if (*settingValue != '=') {
    return SettingResult::NotFound;
  }

  ++settingValue;
  while (*settingValue == ' ' || *settingValue == '\t') {
    ++settingValue;
  }
  return parseUnsigned(settingValue, value) ? SettingResult::Valid
                                            : SettingResult::Invalid;
}

SettingResult parseTextSetting(char *line, const char *key,
                               const char *&value) {
  const size_t keyLength = strlen(key);
  if (strncmp(line, key, keyLength) != 0) {
    return SettingResult::NotFound;
  }

  char *settingValue = line + keyLength;
  while (*settingValue == ' ' || *settingValue == '\t') {
    ++settingValue;
  }
  if (*settingValue != '=') {
    return SettingResult::NotFound;
  }

  ++settingValue;
  while (*settingValue == ' ' || *settingValue == '\t') {
    ++settingValue;
  }

  char *end = settingValue + strlen(settingValue);
  while (end > settingValue &&
         (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
    --end;
  }
  *end = '\0';
  if (*settingValue == '\0') {
    return SettingResult::Invalid;
  }

  value = settingValue;
  return SettingResult::Valid;
}

bool equalsIgnoreCase(const char *left, const char *right) {
  while (*left != '\0' && *right != '\0') {
    char leftChar = *left;
    char rightChar = *right;
    if (leftChar >= 'A' && leftChar <= 'Z') {
      leftChar = static_cast<char>(leftChar - 'A' + 'a');
    }
    if (rightChar >= 'A' && rightChar <= 'Z') {
      rightChar = static_cast<char>(rightChar - 'A' + 'a');
    }
    if (leftChar != rightChar) {
      return false;
    }
    ++left;
    ++right;
  }
  return *left == '\0' && *right == '\0';
}

bool parseBoolean(const char *value, bool &parsed) {
  if (strcmp(value, "1") == 0 || equalsIgnoreCase(value, "yes") ||
      equalsIgnoreCase(value, "true") || equalsIgnoreCase(value, "on")) {
    parsed = true;
    return true;
  }
  if (strcmp(value, "0") == 0 || equalsIgnoreCase(value, "no") ||
      equalsIgnoreCase(value, "false") || equalsIgnoreCase(value, "off")) {
    parsed = false;
    return true;
  }
  return false;
}

bool isValidConfigHost(const char *host) {
  const size_t length = strlen(host);
  if (length == 0 || length > MAX_CONFIG_HOST_LENGTH) {
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    const char value = host[i];
    const bool alphaNumeric =
        (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9');
    if (!alphaNumeric && value != '.' && value != '-' && value != ':' &&
        value != '[' && value != ']') {
      return false;
    }
  }
  return true;
}

bool parseConfig(char *config, ParsedConfig &parsed) {
  char *line = config;
  bool structurallyValid = true;

  while (*line != '\0') {
    char *nextLine = strchr(line, '\n');
    if (nextLine != nullptr) {
      *nextLine = '\0';
    }

    while (*line == ' ' || *line == '\t' || *line == '\r') {
      ++line;
    }

    if (*line != '#' && *line != '\0') {
      uint32_t value = 0;
      SettingResult result = parseSetting(line, "HB_INTERVAL_MIN", value);
      if (result != SettingResult::NotFound) {
        if (parsed.heartbeatIntervalPresent) {
          structurallyValid = false;
        }
        parsed.heartbeatIntervalPresent = true;
        parsed.heartbeatIntervalValid =
            result == SettingResult::Valid &&
            value >= MIN_HEARTBEAT_INTERVAL_MIN &&
            value <= MAX_HEARTBEAT_INTERVAL_MIN;
        if (parsed.heartbeatIntervalValid) {
          parsed.heartbeatIntervalMin = static_cast<uint16_t>(value);
        }
      }

      const char *textValue = nullptr;
      result = parseTextSetting(line, "TUNNEL_ENABLED", textValue);
      if (result != SettingResult::NotFound) {
        if (parsed.tunnelEnabledPresent) {
          structurallyValid = false;
        }
        parsed.tunnelEnabledPresent = true;
        parsed.tunnelEnabledValid =
            result == SettingResult::Valid &&
            parseBoolean(textValue, parsed.tunnelEnabled);
      }

      textValue = nullptr;
      result = parseTextSetting(line, "REMOTE_BIND_HOST", textValue);
      if (result != SettingResult::NotFound) {
        if (parsed.remoteBindHostPresent) {
          structurallyValid = false;
        }
        parsed.remoteBindHostPresent = true;
        parsed.remoteBindHostValid =
            result == SettingResult::Valid && isValidConfigHost(textValue);
        if (parsed.remoteBindHostValid) {
          parsed.remoteBindHost = textValue;
        }
      }

      value = 0;
      result = parseSetting(line, "REMOTE_BIND_PORT", value);
      if (result != SettingResult::NotFound) {
        if (parsed.remoteBindPortPresent) {
          structurallyValid = false;
        }
        parsed.remoteBindPortPresent = true;
        parsed.remoteBindPortValid =
            result == SettingResult::Valid && value >= 1 && value <= 65535;
        if (parsed.remoteBindPortValid) {
          parsed.remoteBindPort = static_cast<uint16_t>(value);
        }
      }

      textValue = nullptr;
      result = parseTextSetting(line, "LOCAL_HOST", textValue);
      if (result != SettingResult::NotFound) {
        if (parsed.localHostPresent) {
          structurallyValid = false;
        }
        parsed.localHostPresent = true;
        parsed.localHostValid =
            result == SettingResult::Valid && isValidConfigHost(textValue);
        if (parsed.localHostValid) {
          parsed.localHost = textValue;
        }
      }

      value = 0;
      result = parseSetting(line, "LOCAL_PORT", value);
      if (result != SettingResult::NotFound) {
        if (parsed.localPortPresent) {
          structurallyValid = false;
        }
        parsed.localPortPresent = true;
        parsed.localPortValid =
            result == SettingResult::Valid && value >= 1 && value <= 65535;
        if (parsed.localPortValid) {
          parsed.localPort = static_cast<uint16_t>(value);
        }
      }
    }

    if (nextLine == nullptr) {
      break;
    }
    line = nextLine + 1;
  }
  return structurallyValid;
}

void processCandidateConfig(const ParsedConfig &parsed) {
  const bool anyCandidateField =
      parsed.tunnelEnabledPresent || parsed.remoteBindHostPresent ||
      parsed.remoteBindPortPresent || parsed.localHostPresent ||
      parsed.localPortPresent;
  if (!anyCandidateField) {
    return;
  }

  if (!parsed.tunnelEnabledPresent || !parsed.remoteBindHostPresent ||
      !parsed.remoteBindPortPresent || !parsed.localHostPresent ||
      !parsed.localPortPresent || !parsed.tunnelEnabledValid ||
      !parsed.remoteBindHostValid || !parsed.remoteBindPortValid ||
      !parsed.localHostValid || !parsed.localPortValid) {
    LOG_W("MINIS", "Tunnel config candidate is incomplete or invalid");
    return;
  }

  const bool changed =
      !candidateTunnelKnown ||
      parsed.tunnelEnabled != candidateTunnelEnabled ||
      candidateRemoteBindHost != parsed.remoteBindHost ||
      parsed.remoteBindPort != candidateRemoteBindPort ||
      candidateLocalHost != parsed.localHost ||
      parsed.localPort != candidateLocalPort;
  if (!changed) {
    LOG_I("MINIS", "Tunnel config candidate unchanged");
    return;
  }

  candidateTunnelKnown = true;
  candidateTunnelEnabled = parsed.tunnelEnabled;
  candidateRemoteBindHost = parsed.remoteBindHost;
  candidateRemoteBindPort = parsed.remoteBindPort;
  candidateLocalHost = parsed.localHost;
  candidateLocalPort = parsed.localPort;

  LOGF_I("MINIS",
         "Tunnel candidate changed (not active): enabled=%s "
         "remote=%s:%u local=%s:%u",
         candidateTunnelEnabled ? "yes" : "no",
         candidateRemoteBindHost.c_str(),
         static_cast<unsigned int>(candidateRemoteBindPort),
         candidateLocalHost.c_str(),
         static_cast<unsigned int>(candidateLocalPort));
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

  ParsedConfig parsed;
  if (configLength == 0 || !parseConfig(config, parsed)) {
    LOG_W("MINIS", "cfg.txt contains duplicate or malformed settings");
    return;
  }

  if (!parsed.heartbeatIntervalPresent || !parsed.heartbeatIntervalValid) {
    LOGF_W("MINIS", "cfg.txt has no valid HB_INTERVAL_MIN; keeping %u min",
           static_cast<unsigned int>(heartbeatIntervalMin));
  } else if (parsed.heartbeatIntervalMin != heartbeatIntervalMin) {
    LOGF_I("MINIS", "HB_INTERVAL_MIN changed: %u -> %u",
           static_cast<unsigned int>(heartbeatIntervalMin),
           static_cast<unsigned int>(parsed.heartbeatIntervalMin));
    heartbeatIntervalMin = parsed.heartbeatIntervalMin;
  } else {
    LOGF_I("MINIS", "HB_INTERVAL_MIN: %u",
           static_cast<unsigned int>(heartbeatIntervalMin));
  }

  processCandidateConfig(parsed);
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
