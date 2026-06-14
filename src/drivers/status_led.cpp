#include "drivers/status_led.h"

#include "app/config.h"

namespace yappl {

bool StatusLed::begin() {
  pinMode(AppConfig::ledPin, OUTPUT);
  // Start dark so boot does not leave the LED in an unknown state.
  set(false);
  started_ = true;
  return true;
}

void StatusLed::set(bool on) {
  if (!started_) {
    return;
  }

  // HIGH turns the LED on for the current wiring.
  digitalWrite(AppConfig::ledPin, on ? HIGH : LOW);
}

void StatusLed::setBrightness(uint8_t brightness) {
  if (!started_) {
    return;
  }

  // Arduino's ESP32 analogWrite uses LEDC PWM under the hood.
  analogWrite(AppConfig::ledPin, brightness);
}

}  // namespace yappl
