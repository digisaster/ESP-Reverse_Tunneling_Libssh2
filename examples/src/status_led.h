#pragma once

#include <Arduino.h>

namespace status_led {

enum class State : uint8_t {
  Booting,
  MissingConfig,
  Setup,
  Connecting,
  Connected,
  Error,
};

void begin();
void set(State state);
void update();

} // namespace status_led
