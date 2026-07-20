#pragma once

#include <Arduino.h>

namespace yappl {

// Centralized hardware map and behavior tuning. This file is the first place to
// edit when changing pins, fake time, task rates, or state durations.
struct AppConfig {
  // This project targets an ESP32-S3 module with 16 MB flash and 8 MB OPI
  // PSRAM. Boot stops early if a different module is selected accidentally.
  static constexpr uint32_t requiredFlashBytes = 16 * 1024 * 1024;
  static constexpr uint32_t requiredPsramBytes = 8 * 1024 * 1024;
  static constexpr bool requirePsram = true;
  // INMP441 sample rate. 16 kHz is enough for speech experiments and keeps
  // buffers smaller than 44.1/48 kHz audio.
  static constexpr uint32_t sampleRateHz = 16000;

  // Number of mic samples read per sensor task pass. At 16 kHz, 256 samples is
  // about 16 ms of audio. Larger values smooth the meter but block longer.
  static constexpr size_t micSampleCount = 256;

  // Audio uploaded to the backend is converted from INMP441 32-bit I2S slots
  // into signed 16-bit mono PCM. The mic task writes into a PSRAM rolling
  // buffer; the network task drains 8 KB batches. At 16 kHz mono 16-bit, 8 KB
  // is about 256 ms of audio and the 96 KB rolling buffer is about 3 seconds.
  static constexpr size_t audioUploadBatchBytes = 8 * 1024;
  static constexpr size_t audioRollingBufferBytes = 96 * 1024;

  // Simple digital gain applied only to uploaded recording audio. INMP441
  // speech at bedside distance can be quiet after converting 24-bit samples to
  // 16-bit PCM, so this boosts before clipping into int16_t.
  static constexpr int audioUploadGain = 4;

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

  // ESP32 LED PWM settings. LEDC is the ESP32 hardware PWM peripheral.
  // Keeping this explicit avoids Arduino analogWrite auto-setup problems.
  static constexpr uint8_t ledPwmChannel = 0;
  static constexpr uint32_t ledPwmFrequencyHz = 5000;
  static constexpr uint8_t ledPwmResolutionBits = 8;

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

  // Step-one internet flag. For now this only connects to Wi-Fi at boot and
  // prints status/IP over Serial; it does not call any cloud API yet.
  static constexpr bool enableWifi = true;

  // Backend API flag. When enabled and configured in include/secrets.h, Yappl
  // periodically pings the backend and reports completed yap sessions.
  static constexpr bool enableBackend = true;

  // HTTP requests run in the network task, not the timing-sensitive output
  // task. Keep this timeout short so a dead backend only delays status updates.
  static constexpr uint32_t backendHttpTimeoutMs = 2500;

  // How often the device sends a normal "I am alive" ping to the backend.
  static constexpr uint32_t backendPingPeriodMs = 30000;

  // How often the device asks the backend for device status.
  static constexpr uint32_t backendStatusPeriodMs = 30000;

  // Network task cadence. Audio upload is still HTTP-per-chunk, so this needs
  // to be much faster than once per second or recordings become very short.
  static constexpr uint32_t networkTaskPeriodMs = 100;

  // Limit each network task pass so audio uploads do not starve status pings.
  // Each batch is 8 KB, so one batch per 100 ms can move much faster than the
  // 32 KB/sec produced by the mic while staying simple.
  static constexpr uint8_t audioUploadBatchesPerNetworkPass = 1;

  // How long boot should wait for Wi-Fi before continuing local behavior.
  // If Wi-Fi fails, Yappl still runs the current local routine.
  static constexpr uint32_t wifiConnectTimeoutMs = 10000;

  // Online time sync. NTP asks internet time servers for the current UTC time,
  // then the TZ string converts it into local Pacific time with daylight saving.
  static constexpr bool enableTimeSync = true;
  static constexpr uint32_t timeSyncTimeoutMs = 8000;
  static constexpr const char *ntpServer1 = "pool.ntp.org";
  static constexpr const char *ntpServer2 = "time.nist.gov";
  static constexpr const char *timeZone = "PST8PDT,M3.2.0/2,M11.1.0/2";

  // Journal period rules. A period starts at 8 PM and ends at 6 AM, but the
  // ready/reminder state intentionally continues after 6 AM if the user missed
  // that period. At the next 8 PM, a fresh period starts and Yappl becomes
  // ready again unless a yap happens in that new period.
  static constexpr uint8_t journalPeriodStartHour = 20;
  static constexpr uint8_t journalPeriodEndHour = 6;

  // Visual idle mode switches to night styling during this local time window.
  static constexpr uint8_t nightStartHour = 20;
  static constexpr uint8_t nightEndHour = 8;

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

  // Sensor task reads button, photoresistor, and mic. 256 samples at 16 kHz is
  // 16 ms of audio, so this period must be about 16 ms to avoid sped-up
  // recordings caused by missing gaps between chunks.
  static constexpr uint32_t sensorTaskPeriodMs = 16;

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

  // Debug/product escape hatch: while sitting in an idle/sleeping state,
  // holding the button this long clears any stored completion and starts a new
  // session. If no stored completion exists, the clear is simply a no-op.
  static constexpr uint32_t clearYapAndReactivateHoldMs = 5000;

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

  // FreeRTOS stack sizes and priorities. Larger priority number wins.
  // Increase a stack size if logs show stack overflow or task crashes.
  static constexpr uint32_t outputTaskStackBytes = 3072;
  static constexpr uint32_t sensorTaskStackBytes = 4096;
  static constexpr uint32_t displayTaskStackBytes = 6144;
  static constexpr uint32_t networkTaskStackBytes = 8192;

  // Output > sensor > display/network keeps LED/piezo responsive while OLED and
  // HTTP work happen in slower background tasks.
  static constexpr UBaseType_t outputTaskPriority = 3;
  static constexpr UBaseType_t sensorTaskPriority = 2;
  static constexpr UBaseType_t displayTaskPriority = 1;
  static constexpr UBaseType_t networkTaskPriority = 1;

  // Piezo off marker. Piezo volume is handled electrically for now.
  static constexpr uint16_t piezoSilentHz = 0;
};

}  // namespace yappl
