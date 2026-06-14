#pragma once

#include <Arduino.h>

#include "assets/face_bitmaps.h"

namespace yappl {

// One animation frame. It says which bitmap to draw, how long to hold it, and
// whether to offset/invert it for simple motion effects.
struct FaceFrame {
  FaceBitmapId bitmapId;
  uint16_t durationMs;
  int8_t xOffset;
  int8_t yOffset;
  bool invert;
};

// A named OLED routine/act. Acts can loop forever as a state's default face, or
// play once as an occasional override such as blink/shake/nod.
struct FaceAct {
  const char *name;
  const FaceFrame *frames;
  uint8_t frameCount;
  bool loop;
  // Optional acts cannot replay until minDowntimeMs has passed. After that,
  // chancePercent is tested by ActPlayer each display tick.
  uint32_t minDowntimeMs;
  uint8_t chancePercent;
};

// Acts from docs/prompts/behaviore.txt. States choose one default act and may
// allow optional acts for variety.
enum class FaceActId : uint8_t {
  IdleStraight,
  Blink,
  LookLeftRight,
  ReminderAnxious,
  ReminderShake,
  NotYetHeadShake,
  ActivationDance,
  ListeningStraight,
  ListeningNod,
  DeactivationSleep,
  IdleNightSleep,
};

// Look up animation metadata by ID.
const FaceAct &faceAct(FaceActId id);

}  // namespace yappl
