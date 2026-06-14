#include "app/yappl_app.h"

#include <algorithm>
#include <limits>
#include <math.h>

#include <esp_heap_caps.h>

#include "app/config.h"

namespace yappl {
namespace {

// INMP441 gives signed 24-bit audio inside a 32-bit I2S slot. Shifting right by
// 8 converts the slot into the normal +/-8388607-ish sample range.
constexpr uint8_t kInmp441SlotShift = 8;

// Full turn in radians, used for sine/cosine animation curves.
constexpr float kTwoPi = 6.28318530718f;

// One-shot melodies for state transitions. The output task indexes these by
// elapsed time, so no delay() is needed while notes play.
constexpr uint16_t kActivationMelodyHz[] = {523, 659, 784, 1047};
constexpr uint16_t kDeactivationMelodyHz[] = {523, 440, 392, 330, 262};
constexpr uint32_t kActivationNoteMs = 140;
constexpr uint32_t kDeactivationNoteMs = 360;

// Return a smooth 0..maxBrightness breathing curve. This is used for reminder
// LED behavior because cosine eases in/out better than a linear ramp.
uint8_t cosineBreath(uint32_t elapsedMs, uint32_t periodMs, uint8_t maxBrightness) {
  if (periodMs == 0) {
    return maxBrightness;
  }

  // phase is 0..1 across one period. The cosine expression maps it to 0..1.
  const float phase = static_cast<float>(elapsedMs % periodMs) / static_cast<float>(periodMs);
  const float wave = (1.0f - cosf(phase * kTwoPi)) * 0.5f;
  return static_cast<uint8_t>(wave * maxBrightness);
}

// Human-readable state name used only for serial debugging.
const char *modeName(AppMode mode) {
  switch (mode) {
    case AppMode::IdleDay:
      return "idle_day";
    case AppMode::Reminder:
      return "reminder";
    case AppMode::NotYet:
      return "not_yet";
    case AppMode::Activation:
      return "activation";
    case AppMode::Listening:
      return "listening";
    case AppMode::Deactivation:
      return "deactivation";
    case AppMode::IdleNight:
      return "idle_night";
  }
  return "unknown";
}

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
  mode_ = restingMode();
  publishSensorState(false, lightRaw, lightLevelFromRaw(lightRaw), 0);
  publishOutputState(mode_, 0, 0, 0);

  Serial.println(micReady_ ? F("INMP441 ready") : F("INMP441 failed"));
  Serial.printf("Stub time %02u:%02u, last yap age %u hours, boot mode %s\n",
                AppConfig::stubCurrentHour,
                AppConfig::stubCurrentMinute,
                AppConfig::stubLastYapAgeHours,
                modeName(mode_));

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

bool YapplApp::isNightTime() const {
  // Night wraps across midnight: 20:00..23:59 or 00:00..07:59.
  return AppConfig::stubCurrentHour >= 20 || AppConfig::stubCurrentHour < 8;
}

bool YapplApp::hasYappedRecently() const {
  // A completed session during this boot counts immediately even though the
  // hardcoded timestamp has not changed.
  return sessionCompletedThisBoot_ || AppConfig::stubLastYapAgeHours < 18;
}

AppMode YapplApp::restingMode() const {
  // Resting mode is what the device should be doing when no short transition
  // state is active.
  if (!hasYappedRecently()) {
    return AppMode::Reminder;
  }
  return isNightTime() ? AppMode::IdleNight : AppMode::IdleDay;
}

void YapplApp::enterMode(AppMode mode, uint32_t nowMs) {
  // Ignore redundant transitions so animations/timers do not restart every tick.
  if (mode_ == mode) {
    return;
  }

  mode_ = mode;
  modeStartedAtMs_ = nowMs;

  // Entering Listening starts a new capture buffer. Activation/Deactivation
  // reset the release guard used to avoid immediately ending a fresh session.
  if (mode_ == AppMode::Listening) {
    resetRecording();
    activationButtonReleased_ = false;
  } else if (mode_ == AppMode::Deactivation) {
    activationButtonReleased_ = false;
  } else if (mode_ == AppMode::Activation) {
    activationButtonReleased_ = false;
  }

  Serial.printf("Mode -> %s\n", modeName(mode_));
}

void YapplApp::updateMode(uint32_t nowMs, const AppState &snapshot) {
  const bool pressed = snapshot.buttonPressed;
  // pressedNow means "this tick is the first tick after the button went down."
  const bool pressedNow = pressed && !previousButtonPressed_;
  if (pressedNow) {
    // Used for hold-duration checks in Reminder and Listening.
    buttonPressedAtMs_ = nowMs;
  }
  if (!pressed) {
    // Listening should not immediately see the button still held from
    // Activation as a request to stop; it must be released first.
    activationButtonReleased_ = true;
  }

  switch (mode_) {
    case AppMode::IdleDay:
    case AppMode::IdleNight:
      if (pressedNow) {
        // Pressing while idle is intentionally rejected for now.
        enterMode(AppMode::NotYet, nowMs);
      } else {
        // If hardcoded time/yap settings imply a different resting mode, follow.
        const AppMode rest = restingMode();
        if (rest != mode_) {
          enterMode(rest, nowMs);
        }
      }
      break;

    case AppMode::Reminder:
      // Reminder requires a hold, not a tap, to activate listening.
      if (pressed && nowMs - buttonPressedAtMs_ >= AppConfig::reminderHoldToActivateMs) {
        enterMode(AppMode::Activation, nowMs);
      }
      break;

    case AppMode::NotYet:
      // NotYet is a short cutscene, then returns to whatever resting mode is
      // correct for the current fake time/yap status.
      if (nowMs - modeStartedAtMs_ >= AppConfig::notYetDurationMs) {
        enterMode(restingMode(), nowMs);
      }
      break;

    case AppMode::Activation:
      // Activation ends after the happy tone/dance window.
      if (nowMs - modeStartedAtMs_ >= AppConfig::activationDurationMs) {
        enterMode(AppMode::Listening, nowMs);
      }
      break;

    case AppMode::Listening:
      // Ending a session requires release after activation, then a deliberate
      // long hold while Listening.
      if (activationButtonReleased_ && pressed &&
          nowMs - buttonPressedAtMs_ >= AppConfig::listeningHoldToDeactivateMs) {
        enterMode(AppMode::Deactivation, nowMs);
      }
      break;

    case AppMode::Deactivation:
      // After GOOD NIGHT, mark this boot as yapped and return to IdleDay/Night.
      if (nowMs - modeStartedAtMs_ >= AppConfig::deactivationDurationMs) {
        sessionCompletedThisBoot_ = true;
        enterMode(restingMode(), nowMs);
      }
      break;
  }

  previousButtonPressed_ = pressed;
}

uint8_t YapplApp::ledBrightnessFor(uint32_t nowMs, const AppState &snapshot) const {
  // All LED behavior is state-driven. The output task calls this frequently and
  // then applies the returned PWM brightness.
  const uint32_t elapsed = nowMs - modeStartedAtMs_;

  switch (mode_) {
    case AppMode::Reminder:
      // If the room is lit, gently breathe so the reminder stays soft.
      if (snapshot.lightLevel >= AppConfig::roomLightOffBelowPercent) {
        return cosineBreath(elapsed, AppConfig::reminderLightOnBreathMs, AppConfig::reminderLedMaxBrightness);
      }

      {
        // If the room is dark, use a more urgent repeating pattern:
        // fast breath -> pause -> three quick flashes -> pause.
        const uint32_t flashPairMs = AppConfig::reminderDarkFlashMs * 2;
        const uint32_t cycleMs = AppConfig::reminderDarkBreathMs +
                                 AppConfig::reminderDarkOffMs +
                                 flashPairMs * 3 +
                                 AppConfig::reminderDarkOffMs;
        uint32_t position = elapsed % cycleMs;
        if (position < AppConfig::reminderDarkBreathMs) {
          return cosineBreath(position, AppConfig::reminderDarkBreathMs, AppConfig::reminderLedMaxBrightness);
        }
        position -= AppConfig::reminderDarkBreathMs;
        if (position < AppConfig::reminderDarkOffMs) {
          return 0;
        }
        position -= AppConfig::reminderDarkOffMs;
        if (position < flashPairMs * 3) {
          return (position % flashPairMs) < AppConfig::reminderDarkFlashMs
                     ? AppConfig::reminderLedMaxBrightness
                     : 0;
        }
      }
      return 0;

    case AppMode::Activation:
      // A short excited pulse while the happy tone plays.
      return cosineBreath(elapsed, 500, AppConfig::reminderLedMaxBrightness);

    case AppMode::Listening:
      // Listening is intentionally steady so the device feels attentive.
      return AppConfig::listeningLedBrightness;

    case AppMode::Deactivation:
      // Fade out while the eyes go to sleep.
      if (elapsed >= AppConfig::deactivationDurationMs) {
        return 0;
      }
      return static_cast<uint8_t>(
          AppConfig::listeningLedBrightness -
          (static_cast<uint32_t>(AppConfig::listeningLedBrightness) * elapsed / AppConfig::deactivationDurationMs));

    case AppMode::IdleDay:
    case AppMode::IdleNight:
    case AppMode::NotYet:
      return 0;
  }
  return 0;
}

uint16_t YapplApp::piezoFrequencyFor(uint32_t nowMs) {
  // Piezo behavior is also state-driven. Returning 0 means "be silent."
  const uint32_t elapsed = nowMs - modeStartedAtMs_;
  const uint16_t *melody = nullptr;
  size_t noteCount = 0;
  uint32_t noteMs = 0;

  if (mode_ == AppMode::Activation) {
    // Upward melody: energetic "ready to listen" cue.
    melody = kActivationMelodyHz;
    noteCount = sizeof(kActivationMelodyHz) / sizeof(kActivationMelodyHz[0]);
    noteMs = kActivationNoteMs;
  } else if (mode_ == AppMode::Deactivation) {
    // Downward melody: winding down into GOOD NIGHT.
    melody = kDeactivationMelodyHz;
    noteCount = sizeof(kDeactivationMelodyHz) / sizeof(kDeactivationMelodyHz[0]);
    noteMs = kDeactivationNoteMs;
  } else {
    return 0;
  }

  // Convert elapsed time into a note index without blocking.
  const size_t index = elapsed / noteMs;
  if (index >= noteCount) {
    return 0;
  }
  return melody[index];
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

    // Decide if the product state should change.
    updateMode(nowMs, snapshot);

    // Convert the current state into desired hardware outputs.
    const uint8_t ledBrightness = ledBrightnessFor(nowMs, snapshot);
    const uint16_t piezoFrequencyHz = piezoFrequencyFor(nowMs);
    setLedBrightness(ledBrightness);
    setPiezoFrequency(piezoFrequencyHz);
    publishOutputState(mode_, ledBrightness, piezoFrequencyHz, recordedSamples_ * sizeof(recordingBuffer_[0]));

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
                    modeName(snapshot.mode),
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
