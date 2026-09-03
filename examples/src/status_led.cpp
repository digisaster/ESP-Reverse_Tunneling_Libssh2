#include "status_led.h"

#ifndef ESP32TUN_STATUS_LED_PIN
#define ESP32TUN_STATUS_LED_PIN -1
#endif

#ifndef ESP32TUN_STATUS_LED_ACTIVE_LOW
#define ESP32TUN_STATUS_LED_ACTIVE_LOW 0
#endif

namespace status_led {
namespace {

#if ESP32TUN_STATUS_LED_PIN >= 0
State currentState = State::Booting;
uint32_t stateStartedAt = 0;
uint32_t lastUpdateAt = 0;
bool initialized = false;
bool outputKnown = false;
bool lastOutput = false;

void writeOutput(bool on) {
  if (outputKnown && on == lastOutput)
    return;
  digitalWrite(ESP32TUN_STATUS_LED_PIN,
               on == (ESP32TUN_STATUS_LED_ACTIVE_LOW != 0) ? LOW : HIGH);
  lastOutput = on;
  outputKnown = true;
}

bool outputFor(State state, uint32_t elapsed) {
  switch (state) {
  case State::Booting:
    return true;
  case State::MissingConfig:
    // Announce missing configuration with three flashes, then use the setup
    // pattern while the captive portal remains active.
    if (elapsed < 900)
      return (elapsed < 120) || (elapsed >= 300 && elapsed < 420) ||
             (elapsed >= 600 && elapsed < 720);
    return ((elapsed - 900) % 1000) < 500;
  case State::Setup:
    return (elapsed % 1000) < 500;
  case State::Connecting: {
    const uint32_t phase = elapsed % 2000;
    return (phase < 120) || (phase >= 300 && phase < 420);
  }
  case State::Connected:
    return false;
  case State::Error: {
    const uint32_t phase = elapsed % 2000;
    return (phase < 120) || (phase >= 300 && phase < 420) ||
           (phase >= 600 && phase < 720);
  }
  }
  return false;
}
#endif

} // namespace

void begin() {
#if ESP32TUN_STATUS_LED_PIN >= 0
  pinMode(ESP32TUN_STATUS_LED_PIN, OUTPUT);
  initialized = true;
  stateStartedAt = millis();
  lastUpdateAt = stateStartedAt;
  writeOutput(true);
#endif
}

void set(State state) {
#if ESP32TUN_STATUS_LED_PIN >= 0
  if (!initialized || state == currentState)
    return;
  currentState = state;
  stateStartedAt = millis();
  lastUpdateAt = stateStartedAt;
  writeOutput(outputFor(currentState, 0));
#else
  (void)state;
#endif
}

void update() {
#if ESP32TUN_STATUS_LED_PIN >= 0
  if (!initialized)
    return;
  const uint32_t now = millis();
  if (now - lastUpdateAt < 20)
    return;
  lastUpdateAt = now;
  writeOutput(outputFor(currentState, now - stateStartedAt));
#endif
}

} // namespace status_led
