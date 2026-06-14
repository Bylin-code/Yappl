#include "app/yappl_app.h"

#include <algorithm>

#include <esp_heap_caps.h>

#include "app/config.h"
#include "app/output_patterns.h"

namespace yappl {
namespace {

// INMP441 gives signed 24-bit audio inside a 32-bit I2S slot. Shifting right by
// 8 converts the slot into the normal +/-8388607-ish sample range.
constexpr uint8_t kInmp441SlotShift = 8;

}  // namespace

void YapplApp::begin() {
  // Serial is optional for the product, but essential during bring-up.
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println(F("Yappl starting"));
  randomSeed(micros());

  // The three RTOS tasks share AppState, so access is protected by this mutex.
  stateMutex_ = xSemaphoreCreateMutex();
  if (stateMutex_ == nullptr) {
    Serial.println(F("Failed to create app state mutex"));
    return;
  }

  // Initialize drivers before tasks start. Once tasks are running, drivers are
  // only called from their owning task.
  displayReady_ = AppConfig::enableOled && display_.begin();
  micReady_ = AppConfig::enableMic && mic_.begin(AppConfig::sampleRateHz);

  if (AppConfig::enableLed) {
    led_.begin();
  }
  if (AppConfig::enablePiezo) {
    piezo_.begin();
  }
  if (AppConfig::enablePhotoresistor) {
    photoresistor_.begin();
  }
  if (AppConfig::enableButton) {
    button_.begin();
  }

  // Try to reserve recording memory up front so Listening does not suddenly
  // allocate while the user is talking.
  allocateRecordingBuffer();

  // Seed AppState with enough data for display/output tasks to start cleanly.
  const int lightRaw = AppConfig::enablePhotoresistor ? photoresistor_.readRaw() : 0;
  stateController_.begin(millis());
  publishSensorState(false, lightRaw, lightLevelFromRaw(lightRaw), 0);
  publishOutputState(stateController_.mode(), 0, 0, 0);

  Serial.println(micReady_ ? F("INMP441 ready") : F("INMP441 failed"));
  Serial.printf("Stub time %02u:%02u, last yap age %u hours, boot mode %s\n",
                AppConfig::stubCurrentHour,
                AppConfig::stubCurrentMinute,
                AppConfig::stubLastYapAgeHours,
                StateController::modeName(stateController_.mode()));

  if (!startTasks()) {
    Serial.println(F("Failed to start RTOS tasks"));
  }
}

void YapplApp::update() {
  // Arduino requires loop(), but the actual product behavior is inside RTOS
  // tasks started by begin(). Sleeping here gives CPU time back to FreeRTOS.
  delay(1000);
}

uint8_t YapplApp::lightLevelFromRaw(int raw) const {
  const int dark = AppConfig::photoresistorDarkRaw;
  const int bright = AppConfig::photoresistorBrightRaw;
  if (dark == bright) {
    // Avoid divide-by-zero if calibration values are accidentally equal.
    return 0;
  }

  // Works even if calibration is inverted because the denominator carries the
  // sign. constrain() then clamps any out-of-range real-world readings.
  int32_t level = static_cast<int32_t>(raw - dark) * 100 / (bright - dark);
  level = constrain(level, 0, 100);
  return static_cast<uint8_t>(level);
}

bool YapplApp::startTasks() {
  if (tasksStarted_) {
    return true;
  }

  // xTaskCreate takes a plain function pointer. The static *Entry wrappers
  // below convert that back into this YapplApp instance.
  const BaseType_t outputStarted = xTaskCreate(
      outputTaskEntry,
      "yappl-output",
      AppConfig::outputTaskStackBytes,
      this,
      AppConfig::outputTaskPriority,
      &outputTaskHandle_);

  const BaseType_t sensorStarted = xTaskCreate(
      sensorTaskEntry,
      "yappl-sensor",
      AppConfig::sensorTaskStackBytes,
      this,
      AppConfig::sensorTaskPriority,
      &sensorTaskHandle_);

  const BaseType_t displayStarted = xTaskCreate(
      displayTaskEntry,
      "yappl-display",
      AppConfig::displayTaskStackBytes,
      this,
      AppConfig::displayTaskPriority,
      &displayTaskHandle_);

  // If any task fails to start, do not pretend the RTOS app is running.
  tasksStarted_ = outputStarted == pdPASS && sensorStarted == pdPASS && displayStarted == pdPASS;
  return tasksStarted_;
}

void YapplApp::setLedBrightness(uint8_t brightness) {
  // Avoid redundant analogWrite calls. This keeps the output task cheap.
  if (!AppConfig::enableLed || brightness == lastLedBrightness_) {
    return;
  }

  led_.setBrightness(brightness);
  lastLedBrightness_ = brightness;
}

void YapplApp::setPiezoFrequency(uint16_t frequencyHz) {
  // Avoid restarting tone() unless the note actually changed.
  if (!AppConfig::enablePiezo || frequencyHz == currentPiezoFrequencyHz_) {
    return;
  }

  if (frequencyHz == 0) {
    // 0 is the app's "silent" frequency.
    piezo_.stop();
  } else {
    piezo_.play(frequencyHz);
  }
  currentPiezoFrequencyHz_ = frequencyHz;
}

void YapplApp::allocateRecordingBuffer() {
  // This is a temporary local recording buffer for Listening. It is not
  // persistent and is not uploaded yet.
  if (recordingBuffer_ != nullptr || AppConfig::recordingBufferBytes == 0) {
    return;
  }

  // Prefer PSRAM so normal internal RAM stays available for tasks and drivers.
  recordingBuffer_ = static_cast<int32_t *>(
      heap_caps_malloc(AppConfig::recordingBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (recordingBuffer_ == nullptr) {
    // Current board config reports no PSRAM, so this is expected unless the
    // board/env is changed later.
    Serial.println(F("PSRAM recording buffer unavailable"));
    recordingCapacitySamples_ = 0;
    return;
  }

  recordingCapacitySamples_ = AppConfig::recordingBufferBytes / sizeof(recordingBuffer_[0]);
  Serial.printf("PSRAM recording buffer ready: %u bytes\n", static_cast<unsigned>(AppConfig::recordingBufferBytes));
}

void YapplApp::resetRecording() {
  // Reuse the same allocated buffer; just start writing from the beginning.
  recordedSamples_ = 0;
}

void YapplApp::appendRecordingSamples(const int32_t *samples, size_t sampleCount) {
  // If no PSRAM buffer exists, Listening still works visually but does not store
  // the raw mic stream.
  if (recordingBuffer_ == nullptr || samples == nullptr || sampleCount == 0) {
    return;
  }

  // Clamp to the remaining buffer space so recording cannot write past the end.
  const size_t available = recordingCapacitySamples_ - std::min(recordedSamples_, recordingCapacitySamples_);
  const size_t toCopy = std::min(sampleCount, available);
  if (toCopy == 0) {
    return;
  }

  // Store raw 32-bit I2S slots for now. A later upload path can convert/compress
  // these into a real audio format.
  memcpy(recordingBuffer_ + recordedSamples_, samples, toCopy * sizeof(samples[0]));
  recordedSamples_ += toCopy;
}

uint8_t YapplApp::micLevelFromSamples(const int32_t *samples, size_t sampleCount) const {
  if (samples == nullptr || sampleCount == 0) {
    return 0;
  }

  // Use peak absolute sample value as a simple responsiveness meter.
  int32_t peakVolume = 0;
  for (size_t i = 0; i < sampleCount; ++i) {
    const int32_t sample = samples[i] >> kInmp441SlotShift;
    const int32_t volume = sample < 0 ? -sample : sample;
    peakVolume = std::max(peakVolume, volume);
  }

  // Convert peak value into 0..100 using config thresholds.
  int64_t meterValue = static_cast<int64_t>(peakVolume) - AppConfig::noiseFloor;
  meterValue = std::max<int64_t>(0, meterValue);
  meterValue = std::min<int64_t>(AppConfig::noiseCeiling, meterValue);
  return static_cast<uint8_t>(meterValue * 100 / AppConfig::noiseCeiling);
}

AppState YapplApp::stateSnapshot() {
  // Return a copy of AppState. Callers can safely use the copy after the mutex
  // is released.
  AppState snapshot;
  if (stateMutex_ == nullptr) {
    return snapshot;
  }

  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  snapshot = state_;
  xSemaphoreGive(stateMutex_);
  return snapshot;
}

void YapplApp::publishSensorState(bool buttonPressed, int lightRaw, uint8_t lightLevel, uint8_t micLevel) {
  // Sensor task publishes only input-derived fields. It does not touch mode or
  // output fields.
  if (stateMutex_ == nullptr) {
    return;
  }

  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  state_.buttonPressed = buttonPressed;
  state_.lightRaw = lightRaw;
  state_.lightLevel = lightLevel;
  state_.micLevel = micLevel;
  xSemaphoreGive(stateMutex_);
}

void YapplApp::publishOutputState(AppMode mode,
                                  uint8_t ledBrightness,
                                  uint16_t piezoFrequencyHz,
                                  size_t recordedBytes) {
  // Output task publishes product mode and hardware-output fields.
  if (stateMutex_ == nullptr) {
    return;
  }

  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  state_.mode = mode;
  state_.ledBrightness = ledBrightness;
  state_.piezoFrequencyHz = piezoFrequencyHz;
  state_.recordedBytes = recordedBytes;
  xSemaphoreGive(stateMutex_);
}

void YapplApp::outputTask() {
  // Highest-priority app task. It owns mode transitions plus LED/piezo outputs.
  TickType_t lastWake = xTaskGetTickCount();

  while (true) {
    const uint32_t nowMs = millis();
    // Read the latest input state as a snapshot so this task does not hold the
    // mutex while computing outputs.
    const AppState snapshot = stateSnapshot();

    // Decide if the product state should change. YapplApp reacts only to the
    // transition side effects it owns, such as clearing the recording buffer.
    const bool modeChanged = stateController_.update(nowMs, snapshot);
    const AppMode mode = stateController_.mode();
    if (modeChanged) {
      if (mode == AppMode::Listening) {
        resetRecording();
      }
      Serial.printf("Mode -> %s\n", StateController::modeName(mode));
    }

    // Convert the current state into desired hardware outputs.
    const uint8_t ledBrightness =
        OutputPatterns::ledBrightnessFor(mode, stateController_.modeStartedAtMs(), nowMs, snapshot);
    const uint16_t piezoFrequencyHz =
        OutputPatterns::piezoFrequencyFor(mode, stateController_.modeStartedAtMs(), nowMs);
    setLedBrightness(ledBrightness);
    setPiezoFrequency(piezoFrequencyHz);
    publishOutputState(mode, ledBrightness, piezoFrequencyHz, recordedSamples_ * sizeof(int32_t));

    // Keep a steady task cadence even if one iteration runs a little late.
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(AppConfig::outputTaskPeriodMs));
  }
}

void YapplApp::sensorTask() {
  // Medium-priority app task. It reads physical inputs and mic data. Mic reads
  // may block, which is why this is not part of the output task.
  TickType_t lastWake = xTaskGetTickCount();
  uint32_t lastLogMs = 0;

  while (true) {
    // Button and light reads are fast. Mic read below may take milliseconds.
    const bool buttonPressed = AppConfig::enableButton && button_.isPressed();
    const int lightRaw = AppConfig::enablePhotoresistor ? photoresistor_.readRaw() : 0;
    const uint8_t lightLevel = lightLevelFromRaw(lightRaw);
    uint8_t micLevel = 0;

    if (micReady_) {
      // Read raw mic samples once per sensor tick. During Listening the same
      // samples are also appended to the temporary recording buffer.
      const size_t samplesRead = mic_.read(micSamples_, AppConfig::micSampleCount);
      if (samplesRead > 0) {
        micLevel = micLevelFromSamples(micSamples_, samplesRead);
        if (stateSnapshot().mode == AppMode::Listening) {
          appendRecordingSamples(micSamples_, samplesRead);
        }
      }
    }

    publishSensorState(buttonPressed, lightRaw, lightLevel, micLevel);

    // Log after publishing so the serial line reflects the newest state.
    const uint32_t nowMs = millis();
    if (AppConfig::enableSerialLog && nowMs - lastLogMs >= AppConfig::serialLogMs) {
      lastLogMs = nowMs;
      const AppState snapshot = stateSnapshot();
      Serial.printf("mode=%s button=%s light=%u%% mic=%u%% led=%u piezo=%uHz rec=%u\n",
                    StateController::modeName(snapshot.mode),
                    snapshot.buttonPressed ? "down" : "up",
                    snapshot.lightLevel,
                    snapshot.micLevel,
                    snapshot.ledBrightness,
                    snapshot.piezoFrequencyHz,
                    static_cast<unsigned>(snapshot.recordedBytes));
    }

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(AppConfig::sensorTaskPeriodMs));
  }
}

void YapplApp::displayTask() {
  // Lowest-priority app task. OLED I2C transfers are slow, so display work is
  // isolated from timing-sensitive output behavior.
  TickType_t lastWake = xTaskGetTickCount();

  while (true) {
    if (displayReady_) {
      // Copy app state first; the slow draw happens after the mutex is released.
      const AppState snapshot = stateSnapshot();
      // ActPlayer maps AppMode to the correct animated face frame.
      const FaceFrame &frame = actPlayer_.update(snapshot.mode, millis());
      display_.drawFaceFrame(frame);
    }

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(AppConfig::displayTaskPeriodMs));
  }
}

void YapplApp::outputTaskEntry(void *context) {
  static_cast<YapplApp *>(context)->outputTask();
}

void YapplApp::sensorTaskEntry(void *context) {
  static_cast<YapplApp *>(context)->sensorTask();
}

void YapplApp::displayTaskEntry(void *context) {
  static_cast<YapplApp *>(context)->displayTask();
}

}  // namespace yappl
