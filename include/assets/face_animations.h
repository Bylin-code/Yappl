#pragma once

#include <Arduino.h>

#include "assets/face_bitmaps.h"

namespace yappl {

struct FaceFrame {
  FaceBitmapId bitmapId;
  uint16_t durationMs;
  int8_t xOffset;
  int8_t yOffset;
  bool invert;
};

struct FaceAct {
  const char *name;
  const FaceFrame *frames;
  uint8_t frameCount;
  bool loop;
  uint32_t minDowntimeMs;
  uint8_t chancePercent;
};

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

const FaceAct &faceAct(FaceActId id);

}  // namespace yappl
