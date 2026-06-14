#include "app/state_controller.h"

#include "app/config.h"

namespace yappl {

void StateController::begin(uint32_t nowMs) {
  // Pick the correct boot mode from the temporary hardcoded time/yap settings.
  mode_ = restingMode();
  modeStartedAtMs_ = nowMs;
}

bool StateController::update(uint32_t nowMs, const AppState &snapshot) {
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
      if (pressedNow) {
        enterMode(AppMode::NotYet, nowMs);
      } else {
        const AppMode rest = restingMode();
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
        enterMode(restingMode(), nowMs);
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
        enterMode(AppMode::Deactivation, nowMs);
      }
      break;

    case AppMode::Deactivation:
      if (nowMs - modeStartedAtMs_ >= AppConfig::deactivationDurationMs) {
        sessionCompletedThisBoot_ = true;
        enterMode(restingMode(), nowMs);
      }
      break;
  }

  previousButtonPressed_ = pressed;
  return before != mode_;
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

bool StateController::isNightTime() const {
  // Temporary clock rule until real Wi-Fi/NTP time exists.
  return AppConfig::stubCurrentHour >= 20 || AppConfig::stubCurrentHour < 8;
}

bool StateController::hasYappedRecently() const {
  // A completed session during this boot counts immediately.
  return sessionCompletedThisBoot_ || AppConfig::stubLastYapAgeHours < 18;
}

AppMode StateController::restingMode() const {
  if (!hasYappedRecently()) {
    return AppMode::Reminder;
  }
  return isNightTime() ? AppMode::IdleNight : AppMode::IdleDay;
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
