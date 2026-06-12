#include "drivers/status_led.h"

#include "app/config.h"

namespace yappl {

bool StatusLed::begin() {
  pinMode(AppConfig::ledPin, OUTPUT);
  set(false);
  started_ = true;
  return true;
}

void StatusLed::set(bool on) {
  if (!started_) {
    return;
  }

  digitalWrite(AppConfig::ledPin, on ? HIGH : LOW);
}

void StatusLed::setBrightness(uint8_t brightness) {
  if (!started_) {
    return;
  }

  analogWrite(AppConfig::ledPin, brightness);
}

}  // namespace yappl
