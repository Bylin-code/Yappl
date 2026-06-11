#pragma once

#include <Arduino.h>
#include <driver/i2s.h>

namespace yappl {

struct MicLevelStats {
  int32_t minimum = 0;
  int32_t maximum = 0;
  int32_t span = 0;
  uint8_t level = 0;
};

class Inmp441Microphone {
 public:
  bool begin(uint32_t sampleRateHz);
  size_t read(int32_t *samples, size_t sampleCount);
  bool readLevel(int32_t *scratch, size_t sampleCount, MicLevelStats &stats);
  void end();

 private:
  bool started_ = false;
  uint32_t sampleRateHz_ = 0;
};

}  // namespace yappl
