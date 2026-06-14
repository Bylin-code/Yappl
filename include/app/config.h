#pragma once

#include <Arduino.h>

namespace yappl {

// Centralized hardware map and behavior tuning. This file is the first place to
// edit when changing pins, fake time, task rates, or state durations.
struct AppConfig {
  // INMP441 sample rate. 16 kHz is enough for speech experiments and keeps
  // buffers smaller than 44.1/48 kHz audio.
  static constexpr uint32_t sampleRateHz = 16000;

  // Number of mic samples read per sensor task pass. At 16 kHz, 256 samples is
  // about 16 ms of audio. Larger values smooth the meter but block longer.
  static constexpr size_t micSampleCount = 256;

  // Simple mic meter scaling. Values below noiseFloor are treated as silence;
  // noiseCeiling maps to 100%. This is not calibrated SPL.
  static constexpr int noiseFloor = 0;
  static constexpr int noiseCeiling = 100000;

  // SH1107 OLED I2C wiring.
  static constexpr uint8_t oledAddress = 0x3C;
  static constexpr int oledSclPin = 11;
  static constexpr int oledSdaPin = 12;

  // INMP441 I2S input wiring. LR is wired to GND, so the driver reads left.
  static constexpr int micBclkPin = 4;
  static constexpr int micLrclkPin = 5;
  static constexpr int micDataPin = 6;

  // Simple IO wiring.
  static constexpr int ledPin = 15;
  static constexpr int piezoPin = 16;
  static constexpr int photoresistorPin = 8;

  // Button uses an external 10k pulldown, so pressed reads HIGH.
  static constexpr int buttonPin = 3;

  // Feature flags for isolating timing/load problems without deleting code. For
  // example, set enableOled=false to test if OLED I2C is causing jitter/noise.
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
  // Hour is 0-23. Minute is 0-59.
  static constexpr uint8_t stubCurrentHour = 21;
  static constexpr uint8_t stubCurrentMinute = 30;

  // Fake age of the last completed yap session. 18+ hours triggers Reminder.
  // Under 18 hours triggers IdleDay or IdleNight depending on stubCurrentHour.
  static constexpr uint8_t stubLastYapAgeHours = 20;

  // Tune these after logging real readings from the installed light sensor.
  // With the suggested divider, brighter light should produce a higher value.
  // These raw values map the ADC reading into a 0-100 light percentage.
  static constexpr int photoresistorDarkRaw = 0;
  static constexpr int photoresistorBrightRaw = 4095;

  // Reminder treats the room as dark below this mapped light percentage.
  static constexpr uint8_t roomLightOffBelowPercent = 15;

  // RTOS periods. Output is intentionally fastest so OLED I2C transfers do not
  // cause visible LED or piezo timing jitter.
  // Output task handles state transitions, LED, and piezo timing.
  static constexpr uint32_t outputTaskPeriodMs = 5;

  // Sensor task reads button, photoresistor, and mic.
  static constexpr uint32_t sensorTaskPeriodMs = 50;

  // Display task sends full OLED frames. Larger is slower but more stable on
  // I2C; smaller is smoother but can stress the display/bus.
  static constexpr uint32_t displayTaskPeriodMs = 200;

  // Serial status print interval.
  static constexpr uint32_t serialLogMs = 500;

  // Short "no, not yet" response after pressing during IdleDay/IdleNight.
  static constexpr uint32_t notYetDurationMs = 1400;

  // Excited transition after holding the button in Reminder.
  static constexpr uint32_t activationDurationMs = 700;

  // Sleep / GOOD NIGHT transition after ending Listening.
  static constexpr uint32_t deactivationDurationMs = 3200;

  // Hold required to move from Reminder into Activation.
  static constexpr uint32_t reminderHoldToActivateMs = 500;

  // Hold required to move from Listening into Deactivation.
  static constexpr uint32_t listeningHoldToDeactivateMs = 1000;

  // Reminder LED brightness cap. 128 is about 50% of 8-bit PWM.
  static constexpr uint8_t reminderLedMaxBrightness = 128;

  // Listening state LED brightness. 128 is about 50% of 8-bit PWM.
  static constexpr uint8_t listeningLedBrightness = 128;

  // Reminder LED when room light is on: slow breath period.
  static constexpr uint32_t reminderLightOnBreathMs = 3000;

  // Reminder LED when room light is off: fast breath, pause, flash sequence.
  static constexpr uint32_t reminderDarkBreathMs = 1000;
  static constexpr uint32_t reminderDarkOffMs = 500;
  static constexpr uint32_t reminderDarkFlashMs = 90;

  // Attempted PSRAM recording capacity for Listening. If this board has no
  // PSRAM, allocation fails cleanly and recording is skipped.
  static constexpr size_t recordingBufferBytes = 256 * 1024;

  // FreeRTOS stack sizes and priorities. Larger priority number wins.
  // Increase a stack size if logs show stack overflow or task crashes.
  static constexpr uint32_t outputTaskStackBytes = 3072;
  static constexpr uint32_t sensorTaskStackBytes = 4096;
  static constexpr uint32_t displayTaskStackBytes = 6144;

  // Output > sensor > display keeps LED/piezo responsive while OLED is slow.
  static constexpr UBaseType_t outputTaskPriority = 3;
  static constexpr UBaseType_t sensorTaskPriority = 2;
  static constexpr UBaseType_t displayTaskPriority = 1;

  // Piezo off marker. Piezo volume is handled electrically for now.
  static constexpr uint16_t piezoSilentHz = 0;
};

}  // namespace yappl
