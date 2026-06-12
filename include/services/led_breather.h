#pragma once

#include <Arduino.h>

#include "drivers/status_led.h"

namespace yappl {

class LedBreather {
 public:
  explicit LedBreather(StatusLed &led);

  void update(uint32_t nowMs, bool active);
  uint8_t brightness() const;

 private:
  StatusLed &led_;
  uint32_t startedAtMs_ = 0;
  uint32_t lastUpdateMs_ = 0;
  uint8_t brightness_ = 0;
  bool active_ = false;

  uint8_t brightnessAt(uint32_t elapsedMs) const;
};

}  // namespace yappl
