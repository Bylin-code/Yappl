#pragma once

#include <Arduino.h>

namespace yappl {

// Small display facade for the SH1107 OLED. The app only depends on these
// high-level drawing methods, so the underlying graphics library can be
// replaced later without changing app behavior.
class OledDisplay {
 public:
  bool begin();
  void clear();
  void drawMeter(uint8_t level);
  void drawHardwareStatus(bool buttonPressed,
                          int lightRaw,
                          uint8_t lightLevel,
                          uint8_t micLevel,
                          uint8_t ledBrightness);

 private:
  void drawFrame();
};

}  // namespace yappl
