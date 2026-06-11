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
  void drawStatus(const __FlashStringHelper *line1, const __FlashStringHelper *line2);
  void drawStatus(const char *line1, const char *line2);
  void sanityCheck();

 private:
  void drawFrame();
};

}  // namespace yappl
