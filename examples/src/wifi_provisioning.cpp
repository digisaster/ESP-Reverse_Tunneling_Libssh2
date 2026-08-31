#include "wifi_provisioning.h"
#include "logger.h"
#include <DNSServer.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>

namespace wifi_provisioning {
namespace {
constexpr char CONFIG_PATH[] = "/esp32tun.cfg";
constexpr char CONFIG_TEMP[] = "/esp32tun.cfg.tmp";
constexpr char KEY_PATH[] = "/esp32tun_ssh_key";
constexpr char KEY_TEMP[] = "/esp32tun_ssh_key.tmp";
constexpr unsigned long WIFI_TIMEOUT_MS = 20000;
constexpr size_t MAX_KEY_SIZE = 16384;

enum class PortalMode { None, Wifi, Device };
WebServer *server = nullptr;
DNSServer *dns = nullptr;
DeviceRuntimeConfig *current = nullptr;
PortalMode mode = PortalMode::None;
bool transitionPending = false;
unsigned long transitionAt = 0;

bool isUnreserved(char c) {
  return isAlphaNumeric(c) || c == '-' || c == '_' || c == '.';
}
char hexDigit(uint8_t n) { return n < 10 ? '0' + n : 'A' + n - 10; }
int hexValue(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}
String encode(const String &value) {
  String result;
  result.reserve(value.length() * 3);
  for (size_t i = 0; i < value.length(); ++i) {
    uint8_t c = value[i];
    if (isUnreserved(c))
      result += static_cast<char>(c);
    else {
      result += '%';
      result += hexDigit(c >> 4);
      result += hexDigit(c & 15);
    }
  }
  return result;
}
bool decode(const String &value, String &result) {
  result = "";
  for (size_t i = 0; i < value.length(); ++i) {
    if (value[i] != '%') {
      result += value[i];
      continue;
    }
    if (i + 2 >= value.length())
      return false;
    int hi = hexValue(value[i + 1]);
    int lo = hexValue(value[i + 2]);
    if (hi < 0 || lo < 0)
      return false;
    result += static_cast<char>((hi << 4) | lo);
    i += 2;
  }
  return true;
}
String escapeHtml(const String &value) {
  String result;
  for (size_t i = 0; i < value.length(); ++i) {
    switch (value[i]) {
    case '&':
      result += F("&amp;");
      break;
    case '<':
      result += F("&lt;");
      break;
    case '>':
      result += F("&gt;");
      break;
    case '"':
      result += F("&quot;");
      break;
    case '\'':
      result += F("&#39;");
      break;
    default:
      result += value[i];
    }
  }
  return result;
}
bool mountStorage() {
  if (LittleFS.begin(false))
    return true;
  LOG_W("SETUP", "Formatting configuration storage");
  return LittleFS.format() && LittleFS.begin(false);
}
bool parsePort(const String &value, int &port) {
  if (value.isEmpty())
    return false;
  for (size_t i = 0; i < value.length(); ++i)
    if (!isDigit(value[i]))
      return false;
  long parsed = value.toInt();
  if (parsed < 1 || parsed > 65535)
    return false;
  port = parsed;
  return true;
}
bool validKey(const String &key) {
  return key.length() > 0 && key.length() <= MAX_KEY_SIZE &&
         key.indexOf("-----BEGIN") >= 0 &&
         key.indexOf("PRIVATE KEY-----") >= 0 && key.indexOf("-----END") >= 0;
}
bool loadKey(String &key) {
  File file = LittleFS.open(KEY_PATH, "r");
  if (!file || file.size() == 0 || file.size() > MAX_KEY_SIZE)
    return false;
  key = file.readString();
  return validKey(key);
}
bool validDeviceConfig(const DeviceRuntimeConfig &c) {
  if (c.sshHost.isEmpty() || c.sshHost.length() > 253 ||
      c.sshUsername.isEmpty() || c.sshUsername.length() > 128 ||
      c.sshPort < 1 || c.sshPort > 65535 || c.remoteBindHost.isEmpty() ||
      c.remoteBindPort < 1 || c.remoteBindPort > 65535 ||
      c.localHost.isEmpty() || c.localPort < 1 || c.localPort > 65535)
    return false;
  return c.sshAuthMethod == SSHAuthMethod::Password ? !c.sshPassword.isEmpty()
                                                    : validKey(c.sshPrivateKey);
}
void writeValue(File &file, const char *name, const String &value) {
  file.print(name);
  file.print('=');
  file.println(encode(value));
}
bool writeConfig(const DeviceRuntimeConfig &c) {
  File file = LittleFS.open(CONFIG_TEMP, "w");
  if (!file)
    return false;
  file.println(F("version=2"));
  file.println(c.setupComplete ? F("setup_complete=1") : F("setup_complete=0"));
  writeValue(file, "wifi_ssid", c.wifiSsid);
  writeValue(file, "wifi_password", c.wifiPassword);
  if (c.setupComplete) {
    writeValue(file, "ssh_host", c.sshHost);
    writeValue(file, "ssh_port", String(c.sshPort));
    writeValue(file, "ssh_user", c.sshUsername);
    writeValue(file, "ssh_auth",
               c.sshAuthMethod == SSHAuthMethod::PrivateKey ? "key"
                                                            : "password");
    writeValue(file, "ssh_password", c.sshPassword);
    writeValue(file, "ssh_key_passphrase", c.sshKeyPassphrase);
    writeValue(file, "remote_host", c.remoteBindHost);
    writeValue(file, "remote_port", String(c.remoteBindPort));
    writeValue(file, "local_host", c.localHost);
    writeValue(file, "local_port", String(c.localPort));
  }
  file.close();
  LittleFS.remove(CONFIG_PATH);
  return LittleFS.rename(CONFIG_TEMP, CONFIG_PATH);
}
bool saveKey(const String &key) {
  File file = LittleFS.open(KEY_TEMP, "w");
  if (!file)
    return false;
  file.print(key);
  file.close();
  LittleFS.remove(KEY_PATH);
  return LittleFS.rename(KEY_TEMP, KEY_PATH);
}
bool loadConfig(DeviceRuntimeConfig &c) {
  File file = LittleFS.open(CONFIG_PATH, "r");
  if (!file)
    return false;
  int version = 0;
  String complete, sshPort, remotePort, localPort, auth;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    if (line.endsWith("\r"))
      line.remove(line.length() - 1);
    int pos = line.indexOf('=');
    if (pos <= 0)
      continue;
    String name = line.substring(0, pos), value;
    if (!decode(line.substring(pos + 1), value))
      return false;
    if (name == "version")
      version = value.toInt();
    else if (name == "setup_complete")
      complete = value;
    else if (name == "wifi_ssid")
      c.wifiSsid = value;
    else if (name == "wifi_password")
      c.wifiPassword = value;
    else if (name == "ssh_host")
      c.sshHost = value;
    else if (name == "ssh_port")
      sshPort = value;
    else if (name == "ssh_user")
      c.sshUsername = value;
    else if (name == "ssh_auth")
      auth = value;
    else if (name == "ssh_password")
      c.sshPassword = value;
    else if (name == "ssh_key_passphrase")
      c.sshKeyPassphrase = value;
    else if (name == "remote_host")
      c.remoteBindHost = value;
    else if (name == "remote_port")
      remotePort = value;
    else if (name == "local_host")
      c.localHost = value;
    else if (name == "local_port")
      localPort = value;
  }
  if ((version != 1 && version != 2) || c.wifiSsid.isEmpty() ||
      c.wifiSsid.length() > 32 || c.wifiPassword.length() > 63)
    return false;
  if (version != 2 || complete != "1")
    return true;
  c.sshAuthMethod =
      auth == "key" ? SSHAuthMethod::PrivateKey : SSHAuthMethod::Password;
  if (!parsePort(sshPort, c.sshPort) ||
      !parsePort(remotePort, c.remoteBindPort) ||
      !parsePort(localPort, c.localPort))
    return true;
  if (c.sshAuthMethod == SSHAuthMethod::PrivateKey && !loadKey(c.sshPrivateKey))
    return true;
  c.setupComplete = validDeviceConfig(c);
  return true;
}
String pageStart(const char *title) {
  String p;
  p.reserve(3072);
  p += F(
      "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' "
      "content='width=device-width,initial-scale=1'><style>body{font-family:"
      "system-ui;max-width:620px;margin:32px auto;padding:0 "
      "16px;background:#f5f7fa;color:#18212b}main{background:#fff;padding:24px;"
      "border-radius:12px}label{display:block;margin-top:16px;font-weight:600}"
      "select,input,textarea,button{box-sizing:border-box;width:100%;padding:"
      "12px;margin-top:6px;border:1px solid "
      "#aeb8c2;border-radius:8px;font:inherit}textarea{min-height:180px;font-"
      "family:monospace}button{background:#1769aa;color:#fff;border:0;font-"
      "weight:700}.note{color:#56616d}.error{color:#a61b1b;font-weight:700}."
      "grid{display:grid;grid-template-columns:2fr "
      "1fr;gap:12px}.hidden{display:none}@media(max-width:520px){.grid{grid-"
      "template-columns:1fr}}</style></head><body><main><h1>");
  p += title;
  p += F("</h1>");
  return p;
}
void addError(String &p, const String &error) {
  if (!error.isEmpty()) {
    p += F("<p class='error'>");
    p += escapeHtml(error);
    p += F("</p>");
  }
}
void sendWifiPage(const String &error = "") {
  String p = pageStart("esp32tun WiFi setup");
  p += F("<p>Select a WiFi network and enter its password.</p>");
  addError(p, error);
  p += F("<form method='post' action='/save-wifi'><label>Detected "
         "network</label><select name='ssid'>");
  for (int i = 0; i < WiFi.scanComplete(); ++i) {
    String ssid = escapeHtml(WiFi.SSID(i));
    p += F("<option value=\"");
    p += ssid;
    p += F("\">");
    p += ssid;
    p += F(" (");
    p += String(WiFi.RSSI(i));
    p += F(" dBm)</option>");
  }
  p += F("</select><label>Hidden or custom SSID</label><input "
         "name='custom_ssid' maxlength='32'><label>WiFi password</label><input "
         "name='password' type='password' maxlength='63'><button>Test and "
         "continue</button></form><p class='note'>The network is tested before "
         "it is stored.</p></main></body></html>");
  server->send(200, "text/html; charset=utf-8", p);
}
void sendDevicePage(const String &error = "") {
  String p = pageStart("esp32tun tunnel setup");
  p += F("<p>Configure the SSH server and one reverse tunnel.</p>");
  addError(p, error);
  p +=
      F("<form method='post' action='/save-device'><div "
        "class='grid'><div><label>SSH server</label><input name='ssh_host' "
        "required maxlength='253'></div><div><label>SSH port</label><input "
        "name='ssh_port' type='number' min='1' max='65535' "
        "value='22'></div></div><label>SSH username</label><input "
        "name='ssh_user' required "
        "maxlength='128'><label>Authentication</label><select id='auth' "
        "name='ssh_auth' onchange='toggleAuth()'><option "
        "value='password'>Password</option><option value='key'>Private "
        "key</option></select><div id='password-fields'><label>SSH "
        "password</label><input name='ssh_password' type='password'></div><div "
        "id='key-fields' class='hidden'><label>Private key</label><textarea "
        "name='ssh_private_key' maxlength='16384' placeholder='-----BEGIN ... "
        "PRIVATE KEY-----'></textarea><label>Key passphrase "
        "(optional)</label><input name='ssh_key_passphrase' "
        "type='password'></div><h2>Reverse tunnel</h2><div "
        "class='grid'><div><label>Remote bind address</label><input "
        "name='remote_host' value='127.0.0.1'></div><div><label>Remote "
        "port</label><input name='remote_port' type='number' min='1' "
        "max='65535' value='23180'></div></div><div "
        "class='grid'><div><label>Local target</label><input name='local_host' "
        "value='192.168.1.1'></div><div><label>Local port</label><input "
        "name='local_port' type='number' min='1' max='65535' "
        "value='22'></div></div><button>Save and start "
        "tunnel</button></form><p class='note'>The setup server is disabled "
        "after restart.</p><script>function toggleAuth(){let "
        "k=document.getElementById('auth').value==='key';document."
        "getElementById('password-fields').className=k?'hidden':'';document."
        "getElementById('key-fields').className=k?'':'hidden'}toggleAuth()</"
        "script></main></body></html>");
  server->send(200, "text/html; charset=utf-8", p);
}
void stopServices() {
  if (dns) {
    dns->stop();
    delete dns;
    dns = nullptr;
  }
  if (server) {
    server->stop();
    delete server;
    server = nullptr;
  }
  mode = PortalMode::None;
}
bool startDevicePortalInternal() {
  server = new WebServer(80);
  if (!server)
    return false;
  server->on("/", HTTP_GET, [] { sendDevicePage(); });
  server->on("/save-device", HTTP_POST, [] {
    DeviceRuntimeConfig c = *current;
    c.sshHost = server->arg("ssh_host");
    c.sshUsername = server->arg("ssh_user");
    c.remoteBindHost = server->arg("remote_host");
    c.localHost = server->arg("local_host");
    if (!parsePort(server->arg("ssh_port"), c.sshPort) ||
        !parsePort(server->arg("remote_port"), c.remoteBindPort) ||
        !parsePort(server->arg("local_port"), c.localPort)) {
      sendDevicePage("Every port must be between 1 and 65535.");
      return;
    }
    c.sshAuthMethod = server->arg("ssh_auth") == "key"
                          ? SSHAuthMethod::PrivateKey
                          : SSHAuthMethod::Password;
    c.sshPassword = server->arg("ssh_password");
    c.sshPrivateKey = server->arg("ssh_private_key");
    c.sshPrivateKey.replace("\r\n", "\n");
    c.sshKeyPassphrase = server->arg("ssh_key_passphrase");
    c.setupComplete = true;
    if (!validDeviceConfig(c)) {
      sendDevicePage(c.sshAuthMethod == SSHAuthMethod::PrivateKey
                         ? "Enter a valid PEM or OpenSSH private key."
                         : "Complete all fields and enter an SSH password.");
      return;
    }
    if (c.sshAuthMethod == SSHAuthMethod::PrivateKey) {
      if (!saveKey(c.sshPrivateKey)) {
        sendDevicePage("The private key could not be stored.");
        return;
      }
      c.sshPassword = "";
    } else {
      LittleFS.remove(KEY_PATH);
      c.sshPrivateKey = "";
      c.sshKeyPassphrase = "";
    }
    if (!writeConfig(c)) {
      sendDevicePage("The configuration could not be stored.");
      return;
    }
    String p = pageStart("Setup complete");
    p += F("<p>The device will restart and start the reverse "
           "tunnel.</p></main></body></html>");
    server->send(200, "text/html; charset=utf-8", p);
    delay(1500);
    ESP.restart();
  });
  server->onNotFound([] { sendDevicePage(); });
  server->begin();
  mode = PortalMode::Device;
  LOGF_I("SETUP", "Tunnel setup: http://%s", WiFi.localIP().toString().c_str());
  return true;
}
void handleWifiSave() {
  String ssid = server->arg("custom_ssid");
  if (ssid.isEmpty())
    ssid = server->arg("ssid");
  String password = server->arg("password");
  if (ssid.isEmpty() || ssid.length() > 32) {
    sendWifiPage("The SSID is missing or too long.");
    return;
  }
  if (!password.isEmpty() &&
      (password.length() < 8 || password.length() > 63)) {
    sendWifiPage("A WiFi password must contain 8 to 63 characters.");
    return;
  }
  WiFi.begin(ssid.c_str(), password.c_str());
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < WIFI_TIMEOUT_MS)
    delay(100);
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect(false, false);
    sendWifiPage("Connection failed. Check the SSID and password.");
    return;
  }
  current->wifiSsid = ssid;
  current->wifiPassword = password;
  current->setupComplete = false;
  if (!writeConfig(*current)) {
    sendWifiPage("WiFi worked, but the configuration could not be stored.");
    return;
  }
  String url = String("http://") + WiFi.localIP().toString() + "/";
  String p = pageStart("WiFi connected");
  p += F(
      "<p>Reconnect to the selected WiFi network, then open:</p><p><a href='");
  p += url;
  p += F("'>");
  p += url;
  p += F("</a></p><p class='note'>This page will continue automatically "
         "when the device becomes reachable on that network.</p><script>"
         "const target='");
  p += url;
  p += F("';function continueSetup(){fetch(target,{mode:'no-cors',cache:"
         "'no-store'}).then(()=>location.replace(target)).catch(()=>"
         "setTimeout(continueSetup,1500))}setTimeout(continueSetup,3500);"
         "</script></main></body></html>");
  server->send(200, "text/html; charset=utf-8", p);
  transitionPending = true;
  transitionAt = millis();
}
bool startWifiPortal() {
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06lX",
           static_cast<unsigned long>(ESP.getEfuseMac() & 0xFFFFFF));
  String name = String("esp32tun-") + suffix;
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(name.c_str()))
    return false;
  LOGF_I("SETUP", "Open WiFi: %s", name.c_str());
  LOGF_I("SETUP", "Open http://%s", WiFi.softAPIP().toString().c_str());
  WiFi.scanNetworks(false, true);
  dns = new DNSServer();
  server = new WebServer(80);
  if (!dns || !server) {
    stopServices();
    return false;
  }
  dns->start(53, "*", WiFi.softAPIP());
  server->on("/save-wifi", HTTP_POST, handleWifiSave);
  server->onNotFound([] { sendWifiPage(); });
  server->begin();
  mode = PortalMode::Wifi;
  return true;
}
} // namespace

bool begin(DeviceRuntimeConfig &config) {
  current = &config;
  if (!mountStorage())
    return false;
  if (loadConfig(config))
    return true;
  return startWifiPortal();
}
bool startDeviceSetup(DeviceRuntimeConfig &config) {
  current = &config;
  return WiFi.status() == WL_CONNECTED && startDevicePortalInternal();
}
bool isActive() { return mode != PortalMode::None; }
void loop() {
  if (mode == PortalMode::Wifi && dns)
    dns->processNextRequest();
  if (server)
    server->handleClient();
  if (transitionPending && millis() - transitionAt >= 2500) {
    transitionPending = false;
    stopServices();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    if (!startDevicePortalInternal())
      LOG_E("SETUP", "Unable to start tunnel setup page");
  }
  delay(2);
}
} // namespace wifi_provisioning
