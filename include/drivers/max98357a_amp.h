#pragma once

#include <Arduino.h>
#include <driver/i2s.h>

namespace yappl {

class Max98357aAmp {
 public:
  bool begin(uint32_t sampleRateHz);
  size_t write(const int16_t *samples, size_t sampleCount);
  bool sanityCheck();
  void end();

 private:
  bool started_ = false;
  uint32_t sampleRateHz_ = 0;
};

}  // namespace yappl
