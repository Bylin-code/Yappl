#include "app/output_patterns.h"

#include <math.h>

#include "app/config.h"

namespace yappl {
namespace OutputPatterns {
namespace {

constexpr float kTwoPi = 6.28318530718f;

constexpr uint16_t kActivationMelodyHz[] = {523, 659, 784, 1047};
constexpr uint16_t kDeactivationMelodyHz[] = {523, 440, 392, 330, 262};
constexpr uint32_t kActivationNoteMs = 140;
constexpr uint32_t kDeactivationNoteMs = 360;

uint8_t cosineBreath(uint32_t elapsedMs, uint32_t periodMs, uint8_t maxBrightness) {
  if (periodMs == 0) {
    return maxBrightness;
  }

  const float phase = static_cast<float>(elapsedMs % periodMs) / static_cast<float>(periodMs);
  const float wave = (1.0f - cosf(phase * kTwoPi)) * 0.5f;
  return static_cast<uint8_t>(wave * maxBrightness);
}

}  // namespace

uint8_t ledBrightnessFor(AppMode mode, uint32_t modeStartedAtMs, uint32_t nowMs, const AppState &snapshot) {
  const uint32_t elapsed = nowMs - modeStartedAtMs;

  switch (mode) {
    case AppMode::Reminder:
      if (snapshot.lightLevel >= AppConfig::roomLightOffBelowPercent) {
        return cosineBreath(elapsed, AppConfig::reminderLightOnBreathMs, AppConfig::reminderLedMaxBrightness);
      }

      {
        const uint32_t flashPairMs = AppConfig::reminderDarkFlashMs * 2;
        const uint32_t cycleMs = AppConfig::reminderDarkBreathMs +
                                 AppConfig::reminderDarkOffMs +
                                 flashPairMs * 3 +
                                 AppConfig::reminderDarkOffMs;
        uint32_t position = elapsed % cycleMs;
        if (position < AppConfig::reminderDarkBreathMs) {
          return cosineBreath(position, AppConfig::reminderDarkBreathMs, AppConfig::reminderLedMaxBrightness);
        }
        position -= AppConfig::reminderDarkBreathMs;
        if (position < AppConfig::reminderDarkOffMs) {
          return 0;
        }
        position -= AppConfig::reminderDarkOffMs;
        if (position < flashPairMs * 3) {
          return (position % flashPairMs) < AppConfig::reminderDarkFlashMs
                     ? AppConfig::reminderLedMaxBrightness
                     : 0;
        }
      }
      return 0;

    case AppMode::Activation:
      return cosineBreath(elapsed, 500, AppConfig::reminderLedMaxBrightness);

    case AppMode::Listening:
      return AppConfig::listeningLedBrightness;

    case AppMode::Deactivation:
      if (elapsed >= AppConfig::deactivationDurationMs) {
        return 0;
      }
      return static_cast<uint8_t>(
          AppConfig::listeningLedBrightness -
          (static_cast<uint32_t>(AppConfig::listeningLedBrightness) * elapsed / AppConfig::deactivationDurationMs));

    case AppMode::IdleDay:
    case AppMode::IdleNight:
    case AppMode::NotYet:
      return 0;
  }

  return 0;
}

uint16_t piezoFrequencyFor(AppMode mode, uint32_t modeStartedAtMs, uint32_t nowMs) {
  const uint32_t elapsed = nowMs - modeStartedAtMs;
  const uint16_t *melody = nullptr;
  size_t noteCount = 0;
  uint32_t noteMs = 0;

  if (mode == AppMode::Activation) {
    melody = kActivationMelodyHz;
    noteCount = sizeof(kActivationMelodyHz) / sizeof(kActivationMelodyHz[0]);
    noteMs = kActivationNoteMs;
  } else if (mode == AppMode::Deactivation) {
    melody = kDeactivationMelodyHz;
    noteCount = sizeof(kDeactivationMelodyHz) / sizeof(kDeactivationMelodyHz[0]);
    noteMs = kDeactivationNoteMs;
  } else {
    return 0;
  }

  const size_t index = elapsed / noteMs;
  if (index >= noteCount) {
    return 0;
  }
  return melody[index];
}

}  // namespace OutputPatterns
}  // namespace yappl
