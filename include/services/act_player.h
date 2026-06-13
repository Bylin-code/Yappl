#pragma once

#include <Arduino.h>

#include "app/app_state.h"
#include "assets/face_animations.h"

namespace yappl {

// Selects and advances OLED acts. States provide the default act; this service
// occasionally overrides that default with configured random acts.
class ActPlayer {
 public:
  const FaceFrame &update(AppMode mode, uint32_t nowMs);

 private:
  AppMode mode_ = AppMode::IdleDay;
  FaceActId currentActId_ = FaceActId::IdleStraight;
  uint8_t frameIndex_ = 0;
  uint32_t frameStartedMs_ = 0;
  uint32_t lastOptionalActMs_[static_cast<uint8_t>(FaceActId::IdleNightSleep) + 1] = {};

  FaceActId defaultActFor(AppMode mode) const;
  void restart(FaceActId actId, uint32_t nowMs);
  void maybeStartOptionalAct(AppMode mode, uint32_t nowMs);
};

}  // namespace yappl
