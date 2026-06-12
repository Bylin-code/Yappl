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

  static constexpr int ledPin = 15;
  static constexpr int piezoPin = 16;
  static constexpr int photoresistorPin = 8;
  static constexpr int buttonPin = 3;

  // Tune these after logging real readings from the installed light sensor.
  // With the suggested divider, brighter light should produce a higher value.
  static constexpr int photoresistorDarkRaw = 0;
  static constexpr int photoresistorBrightRaw = 4095;

  static constexpr uint32_t ledFadePeriodMs = 2000;
  static constexpr uint32_t ledUpdateMs = 5;
  static constexpr uint32_t sensorUpdateMs = 50;
  static constexpr uint32_t displayUpdateMs = 100;
  static constexpr uint32_t serialLogMs = 500;
  static constexpr uint32_t piezoNoteDurationMs = 180;

  // Reserved for a future custom PWM piezo driver. The current driver uses
  // Arduino tone(), so electrical damping controls loudness for now.
  static constexpr uint8_t piezoVolume = 255;
  static constexpr uint8_t piezoPwmChannel = 0;
  static constexpr uint8_t piezoPwmResolutionBits = 8;
};

}  // namespace yappl
