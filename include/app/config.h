#pragma once

#include <Arduino.h>

namespace yappl {

// Centralized hardware map. Keeping pins and tuning constants here makes the
// app layer easy to change without digging through driver code.
struct AppConfig {
  // Mic sampling and meter scaling. The meter is intentionally uncalibrated:
  // it maps the largest recent sample magnitude into a 0-100 UI value.
  static constexpr uint32_t sampleRateHz = 16000;
  static constexpr size_t micSampleCount = 256;

  static constexpr int noiseFloor = 0;
  static constexpr int noiseCeiling = 100000;

  // Current hardware pin map.
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

  // Feature flags for isolating timing/load problems without deleting code.
  static constexpr bool enableOled = true;
  static constexpr bool enableMic = true;
  static constexpr bool enableLed = true;
  static constexpr bool enablePiezo = true;
  static constexpr bool enablePhotoresistor = true;
  static constexpr bool enableButton = true;
  static constexpr bool enableSerialLog = true;

  // Temporary clock placeholders. These are intentionally hardcoded until
  // Wi-Fi/time sync exists; change them before flashing to test different
  // product states.
  static constexpr uint8_t stubCurrentHour = 10;
  static constexpr uint8_t stubCurrentMinute = 30;
  static constexpr uint8_t stubLastYapAgeHours = 20;

  // Tune these after logging real readings from the installed light sensor.
  // With the suggested divider, brighter light should produce a higher value.
  static constexpr int photoresistorDarkRaw = 0;
  static constexpr int photoresistorBrightRaw = 4095;
  static constexpr uint8_t roomLightOffBelowPercent = 15;

  // RTOS periods. Output is intentionally fastest so OLED I2C transfers do not
  // cause visible LED or piezo timing jitter.
  static constexpr uint32_t outputTaskPeriodMs = 5;
  static constexpr uint32_t sensorTaskPeriodMs = 50;
  static constexpr uint32_t displayTaskPeriodMs = 200;
  static constexpr uint32_t serialLogMs = 500;

  static constexpr uint32_t notYetDurationMs = 1400;
  static constexpr uint32_t activationDurationMs = 700;
  static constexpr uint32_t deactivationDurationMs = 3200;
  static constexpr uint32_t reminderHoldToActivateMs = 500;
  static constexpr uint32_t listeningHoldToDeactivateMs = 1000;

  static constexpr uint8_t reminderLedMaxBrightness = 128;
  static constexpr uint8_t listeningLedBrightness = 128;
  static constexpr uint32_t reminderLightOnBreathMs = 3000;
  static constexpr uint32_t reminderDarkBreathMs = 1000;
  static constexpr uint32_t reminderDarkOffMs = 500;
  static constexpr uint32_t reminderDarkFlashMs = 90;

  static constexpr size_t recordingBufferBytes = 256 * 1024;

  // FreeRTOS stack sizes and priorities. Larger priority number wins.
  static constexpr uint32_t outputTaskStackBytes = 3072;
  static constexpr uint32_t sensorTaskStackBytes = 4096;
  static constexpr uint32_t displayTaskStackBytes = 6144;
  static constexpr UBaseType_t outputTaskPriority = 3;
  static constexpr UBaseType_t sensorTaskPriority = 2;
  static constexpr UBaseType_t displayTaskPriority = 1;

  // Piezo is driven through Arduino tone(); volume is handled electrically.
  static constexpr uint16_t piezoSilentHz = 0;
};

}  // namespace yappl
