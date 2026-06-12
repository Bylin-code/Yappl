#pragma once

#include <Arduino.h>

namespace yappl {

// Low-level status LED driver. It exposes direct on/off and raw 8-bit PWM
// brightness; animation patterns belong in services such as LedBreather.
class StatusLed {
 public:
  bool begin();
  void set(bool on);
  void setBrightness(uint8_t brightness);

 private:
  bool started_ = false;
};

}  // namespace yappl
