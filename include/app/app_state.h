#pragma once

#include <Arduino.h>

namespace yappl {

// These are the product-level modes from docs/prompts/behaviore.txt.
// The output task chooses the mode; the display task uses it to choose an act.
enum class AppMode : uint8_t {
  IdleDay,       // Recently yapped, daytime: calm awake face.
  Reminder,      // Has not yapped recently: remind with OLED/LED.
  NotYet,        // Short refusal animation after pressing during idle.
  Activation,    // Short happy transition before listening.
  Listening,     // Mic read/recording state.
  Deactivation,  // Good-night transition after ending listening.
  IdleNight,     // Recently yapped, nighttime: sleeping face.
};

// Small shared snapshot of the system. RTOS tasks pass information through this
// struct instead of directly calling each other.
struct AppState {
  // Product state selected by the output task.
  AppMode mode = AppMode::IdleDay;

  // Network status. This lets the OLED show Wi-Fi state even when Serial logs
  // are unavailable or the USB monitor is flaky.
  bool wifiConnected = false;
  bool backendConnected = false;
  bool timeSynced = false;
  uint8_t currentHour = 0;
  uint8_t currentMinute = 0;

  // Input/sensor values published by the sensor task.
  bool buttonPressed = false;
  int lightRaw = 0;
  uint8_t lightLevel = 0;
  uint8_t micLevel = 0;

  // Output values published by the output task for display/logging.
  uint8_t ledBrightness = 0;
  uint16_t piezoFrequencyHz = 0;

  // Number of bytes captured into the temporary in-RAM/PSRAM recording buffer.
  // This is diagnostic only until upload/storage exists.
  size_t recordedBytes = 0;
};

}  // namespace yappl
