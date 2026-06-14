#pragma once

#include <Arduino.h>

namespace yappl {

// Low-level status LED driver. It exposes direct on/off and raw 8-bit PWM
// brightness; state-specific animation patterns live above the driver.
class StatusLed {
 public:
  // Prepare the GPIO and start with the LED off.
  bool begin();

  // Digital on/off helper.
  void set(bool on);

  // 8-bit PWM brightness: 0 is off, 255 is full brightness.
  void setBrightness(uint8_t brightness);

 private:
  bool started_ = false;
};

}  // namespace yappl
