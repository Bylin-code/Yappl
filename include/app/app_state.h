#pragma once

#include <Arduino.h>

namespace yappl {

enum class AppMode : uint8_t {
  IdleDay,
  Reminder,
  NotYet,
  Activation,
  Listening,
  Deactivation,
  IdleNight,
};

// Small shared snapshot of the system. RTOS tasks pass information through this
// struct instead of directly calling each other.
struct AppState {
  // Product state selected by the output task.
  AppMode mode = AppMode::IdleDay;

  // Input/sensor values published by the sensor task.
  bool buttonPressed = false;
  int lightRaw = 0;
  uint8_t lightLevel = 0;
  uint8_t micLevel = 0;

  // Output values published by the output task for display/logging.
  uint8_t ledBrightness = 0;
  uint16_t piezoFrequencyHz = 0;
  size_t recordedBytes = 0;
};

}  // namespace yappl
