#include "services/led_breather.h"

#include <math.h>

#include "app/config.h"

namespace yappl {
namespace {

constexpr float kTwoPi = 6.28318530718f;

}  // namespace

LedBreather::LedBreather(StatusLed &led) : led_(led) {}

void LedBreather::update(uint32_t nowMs, bool active) {
  if (!active) {
    if (active_ || brightness_ != 0) {
      led_.setBrightness(0);
    }
    active_ = false;
    brightness_ = 0;
    return;
  }

  if (!active_) {
    active_ = true;
    startedAtMs_ = nowMs;
    lastUpdateMs_ = 0;
  }

  if (nowMs - lastUpdateMs_ < AppConfig::ledUpdateMs) {
    return;
  }

  lastUpdateMs_ = nowMs;
  brightness_ = brightnessAt(nowMs - startedAtMs_);
  led_.setBrightness(brightness_);
}

uint8_t LedBreather::brightness() const {
  return brightness_;
}

uint8_t LedBreather::brightnessAt(uint32_t elapsedMs) const {
  const uint32_t periodMs = AppConfig::ledFadePeriodMs;
  if (periodMs == 0) {
    return 255;
  }

  const float phase = static_cast<float>(elapsedMs % periodMs) / static_cast<float>(periodMs);
  const float wave = (1.0f - cosf(phase * kTwoPi)) * 0.5f;
  return static_cast<uint8_t>(wave * 255.0f);
}

}  // namespace yappl
