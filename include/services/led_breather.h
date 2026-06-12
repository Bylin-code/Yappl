#pragma once

#include <Arduino.h>

#include "drivers/status_led.h"

namespace yappl {

// Behavior service that turns a boolean "active" state into a smooth breathing
// LED waveform. The RTOS output task decides how often update() is called.
class LedBreather {
 public:
  explicit LedBreather(StatusLed &led);

  void update(uint32_t nowMs, bool active);
  uint8_t brightness() const;

 private:
  StatusLed &led_;
  // Timestamp of the current active breathing cycle.
  uint32_t startedAtMs_ = 0;
  uint8_t brightness_ = 0;
  bool active_ = false;

  // Computes one point on the breathing curve.
  uint8_t brightnessAt(uint32_t elapsedMs) const;
};

}  // namespace yappl
