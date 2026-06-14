#pragma once

#include <Arduino.h>

#include "assets/face_animations.h"

namespace yappl {

// Small display facade for the SH1107 OLED. The app only depends on these
// high-level drawing methods, so the underlying graphics library can be
// replaced later without changing app behavior.
class OledDisplay {
 public:
  bool begin();
  void clear();

  // Draw exactly one frame produced by ActPlayer. This is the only current OLED
  // rendering path for the product face.
  void drawFaceFrame(const FaceFrame &frame);
};

}  // namespace yappl
