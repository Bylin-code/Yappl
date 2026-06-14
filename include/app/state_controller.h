#pragma once

#include <Arduino.h>

#include "app/app_state.h"

namespace yappl {

// The state machine gets all time/storage facts through this small value object.
// That keeps it testable and prevents it from directly reading NTP or flash.
struct TimeContext {
  bool valid = false;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint64_t nowEpoch = 0;
  bool hasLastYap = false;
  uint64_t lastYapEpoch = 0;
  bool completedThisBoot = false;
};

// Owns the product state machine: current mode, transition timers, and button
// edge/hold tracking. It does not touch hardware directly.
class StateController {
 public:
  void begin(uint32_t nowMs, const TimeContext &time);
  bool update(uint32_t nowMs, const AppState &snapshot, const TimeContext &time);

  AppMode mode() const { return mode_; }
  uint32_t modeStartedAtMs() const { return modeStartedAtMs_; }
  bool consumeSessionCompleted();
  bool consumeClearYapRequested();

  static const char *modeName(AppMode mode);

 private:
  bool isNightTime(const TimeContext &time) const;
  bool hasYappedToday(const TimeContext &time) const;
  bool reminderTimeReached(const TimeContext &time) const;
  AppMode restingMode(const TimeContext &time) const;
  bool enterMode(AppMode mode, uint32_t nowMs);

  bool sessionCompletedPending_ = false;
  bool clearYapRequested_ = false;
  bool previousButtonPressed_ = false;
  bool activationButtonReleased_ = false;
  uint32_t buttonPressedAtMs_ = 0;
  uint32_t modeStartedAtMs_ = 0;
  AppMode mode_ = AppMode::IdleDay;
};

}  // namespace yappl
