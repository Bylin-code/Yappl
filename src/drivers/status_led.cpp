#include "drivers/status_led.h"

#include "app/config.h"

namespace yappl {

bool StatusLed::begin() {
  pinMode(AppConfig::ledPin, OUTPUT);

  // ESP32 PWM is handled by LEDC. We configure it ourselves instead of relying
  // on analogWrite's implicit setup, because the app uses RTOS tasks and should
  // have deterministic hardware initialization.
  ledcSetup(AppConfig::ledPwmChannel, AppConfig::ledPwmFrequencyHz, AppConfig::ledPwmResolutionBits);
  ledcAttachPin(AppConfig::ledPin, AppConfig::ledPwmChannel);
  started_ = true;

  // Start dark so boot does not leave the LED in an unknown state.
  setBrightness(0);
  return true;
}

void StatusLed::set(bool on) {
  if (!started_) {
    return;
  }

  setBrightness(on ? 255 : 0);
}

void StatusLed::setBrightness(uint8_t brightness) {
  if (!started_) {
    return;
  }

  ledcWrite(AppConfig::ledPwmChannel, brightness);
}

}  // namespace yappl
