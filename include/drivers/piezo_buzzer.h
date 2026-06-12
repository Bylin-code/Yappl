#pragma once

#include <Arduino.h>

namespace yappl {

class PiezoBuzzer {
 public:
  bool begin();
  void play(uint16_t frequencyHz);
  void playFor(uint16_t frequencyHz, uint32_t durationMs);
  void stop();

 private:
  bool started_ = false;
};

}  // namespace yappl
