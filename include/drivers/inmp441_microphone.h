#pragma once

#include <Arduino.h>
#include <driver/i2s.h>

namespace yappl {

// Summary produced from one block of mic samples for the OLED meter and logs.
struct MicLevelStats {
  // Lowest signed sample seen in the block.
  int32_t minimum = 0;

  // Highest signed sample seen in the block.
  int32_t maximum = 0;

  // Meter input after floor/ceiling scaling, kept for logs/debugging.
  int32_t span = 0;

  // Final 0-100 value used by UI/debug code.
  uint8_t level = 0;
};

// INMP441 I2S microphone driver. It returns raw 32-bit I2S slots and also
// exposes a simple peak level helper for the current UI.
class Inmp441Microphone {
 public:
  // Configure the ESP32 I2S peripheral for the INMP441.
  bool begin(uint32_t sampleRateHz);

  // Read raw 32-bit I2S slots into caller-owned memory. This may block.
  size_t read(int32_t *samples, size_t sampleCount);

  // Convenience helper for turning one raw sample block into meter stats.
  bool readLevel(int32_t *scratch, size_t sampleCount, MicLevelStats &stats);

  // Stop and free the ESP32 I2S driver.
  void end();

 private:
  bool started_ = false;
  uint32_t sampleRateHz_ = 0;
};

}  // namespace yappl
