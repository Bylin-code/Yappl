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

  // Draw exactly one frame produced by ActPlayer, plus a tiny top-right Wi-Fi
  // status icon so connection state is visible without Serial Monitor.
  void drawFaceFrame(const FaceFrame &frame,
                     bool wifiConnected,
                     bool timeSynced,
                     uint8_t hour,
                     uint8_t minute);
};

}  // namespace yappl
