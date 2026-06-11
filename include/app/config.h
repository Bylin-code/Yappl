#pragma once

#include <Arduino.h>

namespace yappl {

// Centralized hardware map. Keeping pins and tuning constants here makes the
// app layer easy to change without digging through driver code.
struct AppConfig {
  static constexpr uint32_t sampleRateHz = 16000;
  static constexpr size_t micSampleCount = 256;

  static constexpr int noiseFloor = 0;
  static constexpr int noiseCeiling = 100000;

  static constexpr uint8_t oledAddress = 0x3C;
  static constexpr int oledSclPin = 11;
  static constexpr int oledSdaPin = 12;

  static constexpr int micBclkPin = 4;
  static constexpr int micLrclkPin = 5;
  static constexpr int micDataPin = 6;

  static constexpr int ampBclkPin = 15;
  static constexpr int ampLrclkPin = 16;
  static constexpr int ampDataPin = 17;
};

}  // namespace yappl
