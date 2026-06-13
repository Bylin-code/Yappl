#pragma once

#include <Arduino.h>

namespace yappl {

struct FaceBitmap {
  const uint8_t *data;
  uint8_t width;
  uint8_t height;
};

enum class FaceBitmapId : uint8_t {
  EyesStraight,
  EyesBlinkHalf,
  EyesBlinkClosed,
  EyesLookLeft,
  EyesLookRight,
  EyesAnxiousLeft,
  EyesAnxiousRight,
  EyesNotYetLeft,
  EyesNotYetRight,
  EyesDanceUp,
  EyesDanceDown,
  EyesDanceLeft,
  EyesDanceRight,
  EyesNodUp,
  EyesNodDown,
  EyesSleepy1,
  EyesSleepy2,
  EyesSleepClosed,
  EyesGoodNight,
  EyesNightZ1,
  EyesNightZ2,
  EyesNightZ3,
};

const FaceBitmap &faceBitmap(FaceBitmapId id);

}  // namespace yappl
