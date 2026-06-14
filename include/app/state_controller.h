#pragma once

#include <Arduino.h>

#include "app/app_state.h"

namespace yappl {

// Owns the product state machine: current mode, transition timers, and button
// edge/hold tracking. It does not touch hardware directly.
class StateController {
 public:
  void begin(uint32_t nowMs);
  bool update(uint32_t nowMs, const AppState &snapshot);

  AppMode mode() const { return mode_; }
  uint32_t modeStartedAtMs() const { return modeStartedAtMs_; }

  static const char *modeName(AppMode mode);

 private:
  bool isNightTime() const;
  bool hasYappedRecently() const;
  AppMode restingMode() const;
  bool enterMode(AppMode mode, uint32_t nowMs);

  bool sessionCompletedThisBoot_ = false;
  bool previousButtonPressed_ = false;
  bool activationButtonReleased_ = false;
  uint32_t buttonPressedAtMs_ = 0;
  uint32_t modeStartedAtMs_ = 0;
  AppMode mode_ = AppMode::IdleDay;
};

}  // namespace yappl
