#pragma once

#include <Arduino.h>

#include "app/app_state.h"
#include "assets/face_animations.h"

namespace yappl {

// Selects and advances OLED acts. States provide the default act; this service
// occasionally overrides that default with configured random acts.
class ActPlayer {
 public:
  // Advance animation time for the given app mode and return the frame the OLED
  // should draw right now.
  const FaceFrame &update(AppMode mode, uint32_t nowMs);

 private:
  // Current app mode being animated. When mode changes, animation resets to the
  // new mode's default act.
  AppMode mode_ = AppMode::IdleDay;

  // Current act and frame within that act.
  FaceActId currentActId_ = FaceActId::IdleStraight;
  uint8_t frameIndex_ = 0;
  uint32_t frameStartedMs_ = 0;

  // Per-act cooldown memory for random optional acts.
  uint32_t lastOptionalActMs_[static_cast<uint8_t>(FaceActId::IdleNightSleep) + 1] = {};

  FaceActId defaultActFor(AppMode mode) const;
  void restart(FaceActId actId, uint32_t nowMs);
  void maybeStartOptionalAct(AppMode mode, uint32_t nowMs);
};

}  // namespace yappl
