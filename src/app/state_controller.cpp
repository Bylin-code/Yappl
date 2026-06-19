#include "app/state_controller.h"

#include "app/config.h"

namespace yappl {
namespace {

uint64_t latestJournalPeriodStartEpoch(uint64_t nowEpoch) {
  // The product rule is based on the latest 8 PM local start, not midnight.
  // Before 8 PM, the relevant period is the one that started yesterday at 8 PM.
  time_t now = static_cast<time_t>(nowEpoch);
  tm local = {};
  if (localtime_r(&now, &local) == nullptr) {
    return 0;
  }

  local.tm_hour = AppConfig::journalPeriodStartHour;
  local.tm_min = 0;
  local.tm_sec = 0;
  local.tm_isdst = -1;

  time_t start = mktime(&local);
  if (start < 0) {
    return 0;
  }

  if (now < start) {
    start -= 24 * 60 * 60;
  }

  return static_cast<uint64_t>(start);
}

}  // namespace

void StateController::begin(uint32_t nowMs, const TimeContext &time) {
  // Pick the correct boot mode from real clock + stored yap history.
  mode_ = restingMode(time);
  modeStartedAtMs_ = nowMs;
}

bool StateController::update(uint32_t nowMs, const AppState &snapshot, const TimeContext &time) {
  const bool pressed = snapshot.buttonPressed;
  const bool pressedNow = pressed && !previousButtonPressed_;

  if (pressedNow) {
    // The same timestamp is used for hold-to-start and hold-to-stop checks.
    buttonPressedAtMs_ = nowMs;
  }

  if (!pressed) {
    // After Activation, the user must release before a later hold can stop the
    // Listening session. This avoids instantly ending a session.
    activationButtonReleased_ = true;
  }

  const AppMode before = mode_;

  switch (mode_) {
    case AppMode::IdleDay:
    case AppMode::IdleNight:
      if (pressed && nowMs - buttonPressedAtMs_ >= AppConfig::clearYapAndReactivateHoldMs) {
        clearYapRequested_ = true;
        enterMode(AppMode::Activation, nowMs);
      } else if (!pressed && previousButtonPressed_) {
        // A short idle press still gets the "not yet" response. Delaying this
        // until release gives the long-hold reset gesture time to be detected.
        enterMode(AppMode::NotYet, nowMs);
      } else {
        const AppMode rest = restingMode(time);
        if (rest != mode_) {
          enterMode(rest, nowMs);
        }
      }
      break;

    case AppMode::Reminder:
      if (pressed && nowMs - buttonPressedAtMs_ >= AppConfig::reminderHoldToActivateMs) {
        enterMode(AppMode::Activation, nowMs);
      }
      break;

    case AppMode::NotYet:
      if (nowMs - modeStartedAtMs_ >= AppConfig::notYetDurationMs) {
        enterMode(restingMode(time), nowMs);
      }
      break;

    case AppMode::Activation:
      if (nowMs - modeStartedAtMs_ >= AppConfig::activationDurationMs) {
        enterMode(AppMode::Listening, nowMs);
      }
      break;

    case AppMode::Listening:
      if (activationButtonReleased_ && pressed &&
          nowMs - buttonPressedAtMs_ >= AppConfig::listeningHoldToDeactivateMs) {
        // The user's deliberate stop-hold means the session is complete. Mark
        // it immediately instead of waiting for the GOOD NIGHT animation to
        // finish, so the completion survives reset/power loss right away.
        sessionCompletedPending_ = true;
        enterMode(AppMode::Deactivation, nowMs);
      }
      break;

    case AppMode::Deactivation:
      if (nowMs - modeStartedAtMs_ >= AppConfig::deactivationDurationMs) {
        enterMode(isNightTime(time) ? AppMode::IdleNight : AppMode::IdleDay, nowMs);
      }
      break;
  }

  previousButtonPressed_ = pressed;
  return before != mode_;
}

bool StateController::consumeSessionCompleted() {
  if (!sessionCompletedPending_) {
    return false;
  }
  sessionCompletedPending_ = false;
  return true;
}

bool StateController::applyBackendMode(AppMode mode, uint32_t nowMs) {
  if (mode != AppMode::IdleDay && mode != AppMode::IdleNight && mode != AppMode::Reminder) {
    return false;
  }

  if (mode_ != AppMode::IdleDay && mode_ != AppMode::IdleNight && mode_ != AppMode::Reminder && mode_ != AppMode::NotYet) {
    return false;
  }

  return enterMode(mode, nowMs);
}

bool StateController::consumeClearYapRequested() {
  if (!clearYapRequested_) {
    return false;
  }
  clearYapRequested_ = false;
  return true;
}

const char *StateController::modeName(AppMode mode) {
  switch (mode) {
    case AppMode::IdleDay:
      return "idle_day";
    case AppMode::Reminder:
      return "reminder";
    case AppMode::NotYet:
      return "not_yet";
    case AppMode::Activation:
      return "activation";
    case AppMode::Listening:
      return "listening";
    case AppMode::Deactivation:
      return "deactivation";
    case AppMode::IdleNight:
      return "idle_night";
  }
  return "unknown";
}

bool StateController::isNightTime(const TimeContext &time) const {
  if (!time.valid) {
    return false;
  }

  return time.hour >= AppConfig::nightStartHour || time.hour < AppConfig::nightEndHour;
}

bool StateController::hasYappedInCurrentJournalPeriod(const TimeContext &time) const {
  if (!time.valid || !time.hasLastYap) {
    return false;
  }

  const uint64_t periodStart = latestJournalPeriodStartEpoch(time.nowEpoch);
  if (periodStart == 0) {
    return false;
  }

  return time.lastYapEpoch >= periodStart && time.lastYapEpoch <= time.nowEpoch;
}

AppMode StateController::restingMode(const TimeContext &time) const {
  // If time is invalid, avoid nagging. The device cannot know which 8 PM
  // journal period applies yet.
  if (!time.valid) {
    return AppMode::IdleDay;
  }

  if (!hasYappedInCurrentJournalPeriod(time)) {
    return AppMode::Reminder;
  }
  return isNightTime(time) ? AppMode::IdleNight : AppMode::IdleDay;
}

bool StateController::enterMode(AppMode mode, uint32_t nowMs) {
  if (mode_ == mode) {
    return false;
  }

  mode_ = mode;
  modeStartedAtMs_ = nowMs;

  // These transition modes reset the release guard used by Listening.
  if (mode_ == AppMode::Activation || mode_ == AppMode::Listening || mode_ == AppMode::Deactivation) {
    activationButtonReleased_ = false;
  }

  return true;
}

}  // namespace yappl
