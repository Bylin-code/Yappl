#pragma once

#include <Arduino.h>

namespace yappl {

// Passive piezo driver using Arduino tone(). Volume is handled electrically for
// now, not in software.
class PiezoBuzzer {
 public:
  // Prepare the GPIO and force silence.
  bool begin();

  // Start a continuous tone until stop() or another play() call.
  void play(uint16_t frequencyHz);

  // Play a timed tone using Arduino tone() duration support.
  void playFor(uint16_t frequencyHz, uint32_t durationMs);

  // Stop tone generation and drive the pin low.
  void stop();

 private:
  bool started_ = false;
};

}  // namespace yappl
