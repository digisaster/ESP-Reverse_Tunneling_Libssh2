#pragma once
#include <Arduino.h>

enum class SSHAuthMethod { Password, PrivateKey };

struct DeviceRuntimeConfig {
  String wifiSsid;
  String wifiPassword;
  bool setupComplete = false;
  String sshHost;
  int sshPort = 22;
  String sshUsername;
  SSHAuthMethod sshAuthMethod = SSHAuthMethod::Password;
  String sshPassword;
  String sshPrivateKey;
  String sshKeyPassphrase;
  String remoteBindHost = "127.0.0.1";
  int remoteBindPort = 23180;
  String localHost = "192.168.1.1";
  int localPort = 22;
};

namespace wifi_provisioning {
bool begin(DeviceRuntimeConfig &config);
bool startDeviceSetup(DeviceRuntimeConfig &config);
bool isActive();
void loop();
} // namespace wifi_provisioning
