#pragma once

#include <Arduino.h>

namespace yappl {

// Small shared snapshot of the system. RTOS tasks pass information through this
// struct instead of directly calling each other.
struct AppState {
  // Input/sensor values published by the sensor task.
  bool buttonPressed = false;
  int lightRaw = 0;
  uint8_t lightLevel = 0;
  uint8_t micLevel = 0;

  // Output values published by the output task for display/logging.
  uint8_t ledBrightness = 0;
  uint16_t piezoFrequencyHz = 0;
};

}  // namespace yappl
