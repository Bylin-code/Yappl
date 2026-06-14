#pragma once

#include <Arduino.h>

namespace yappl {

// One compiled 1-bit face bitmap. data points at XBM-style bytes in PROGMEM:
// each bit is one OLED pixel, and U8g2 draws it with drawXBMP().
struct FaceBitmap {
  const uint8_t *data;
  uint8_t width;
  uint8_t height;
};

// Stable IDs for generated placeholder face drawings. Higher-level animation
// code refers to IDs instead of raw byte arrays.
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

// Look up bitmap metadata and bytes by ID.
const FaceBitmap &faceBitmap(FaceBitmapId id);

}  // namespace yappl
