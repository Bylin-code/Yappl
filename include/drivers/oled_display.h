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

  // Legacy simple mic-only screen. Kept as a useful display primitive even
  // though the RTOS demo currently uses drawHardwareStatus().
  void drawMeter(uint8_t level);

  // Full current debug/status screen. This call sends a full OLED buffer over
  // I2C, so it should stay in the low-priority display task.
  void drawHardwareStatus(bool buttonPressed,
                          int lightRaw,
                          uint8_t lightLevel,
                          uint8_t micLevel,
                          uint8_t ledBrightness,
                          uint16_t piezoFrequencyHz);

 private:
  // Shared frame for the simple mic meter screen.
  void drawFrame();
};

}  // namespace yappl
