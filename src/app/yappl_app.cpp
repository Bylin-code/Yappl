#include "app/yappl_app.h"

#include <algorithm>
#include <limits>
#include <math.h>

#include <esp_heap_caps.h>

#include "app/config.h"

namespace yappl {
namespace {

constexpr uint8_t kInmp441SlotShift = 8;
constexpr float kTwoPi = 6.28318530718f;

constexpr uint16_t kActivationMelodyHz[] = {523, 659, 784, 1047};
constexpr uint16_t kDeactivationMelodyHz[] = {523, 440, 392, 330, 262};
constexpr uint32_t kActivationNoteMs = 140;
constexpr uint32_t kDeactivationNoteMs = 360;

uint8_t cosineBreath(uint32_t elapsedMs, uint32_t periodMs, uint8_t maxBrightness) {
  if (periodMs == 0) {
    return maxBrightness;
  }

  const float phase = static_cast<float>(elapsedMs % periodMs) / static_cast<float>(periodMs);
  const float wave = (1.0f - cosf(phase * kTwoPi)) * 0.5f;
  return static_cast<uint8_t>(wave * maxBrightness);
}

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
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println(F("Yappl starting"));
  randomSeed(micros());

  stateMutex_ = xSemaphoreCreateMutex();
  if (stateMutex_ == nullptr) {
    Serial.println(F("Failed to create app state mutex"));
    return;
  }

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

  allocateRecordingBuffer();

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
  delay(1000);
}

uint8_t YapplApp::lightLevelFromRaw(int raw) const {
  const int dark = AppConfig::photoresistorDarkRaw;
  const int bright = AppConfig::photoresistorBrightRaw;
  if (dark == bright) {
    return 0;
  }

  int32_t level = static_cast<int32_t>(raw - dark) * 100 / (bright - dark);
  level = constrain(level, 0, 100);
  return static_cast<uint8_t>(level);
}

bool YapplApp::startTasks() {
  if (tasksStarted_) {
    return true;
  }

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

  tasksStarted_ = outputStarted == pdPASS && sensorStarted == pdPASS && displayStarted == pdPASS;
  return tasksStarted_;
}

bool YapplApp::isNightTime() const {
  return AppConfig::stubCurrentHour >= 20 || AppConfig::stubCurrentHour < 8;
}

bool YapplApp::hasYappedRecently() const {
  return sessionCompletedThisBoot_ || AppConfig::stubLastYapAgeHours < 18;
}

AppMode YapplApp::restingMode() const {
  if (!hasYappedRecently()) {
    return AppMode::Reminder;
  }
  return isNightTime() ? AppMode::IdleNight : AppMode::IdleDay;
}

void YapplApp::enterMode(AppMode mode, uint32_t nowMs) {
  if (mode_ == mode) {
    return;
  }

  mode_ = mode;
  modeStartedAtMs_ = nowMs;
  melodyIndex_ = 0;
  lastMelodyNoteMs_ = 0;

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
  const bool pressedNow = pressed && !previousButtonPressed_;
  if (pressedNow) {
    buttonPressedAtMs_ = nowMs;
  }
  if (!pressed) {
    activationButtonReleased_ = true;
  }

  switch (mode_) {
    case AppMode::IdleDay:
    case AppMode::IdleNight:
      if (pressedNow) {
        enterMode(AppMode::NotYet, nowMs);
      } else {
        const AppMode rest = restingMode();
        if (rest != mode_) {
          enterMode(rest, nowMs);
        }
      }
      break;

    case AppMode::Reminder:
      if (pressed && nowMs - buttonPressedAtMs_ >= AppConfig::reminderHoldToActivateMs) {
        enterMode(AppMode::Activation, nowMs);
      }
      break;

    case AppMode::NotYet:
      if (nowMs - modeStartedAtMs_ >= AppConfig::notYetDurationMs) {
        enterMode(restingMode(), nowMs);
      }
      break;

    case AppMode::Activation:
      if (nowMs - modeStartedAtMs_ >= AppConfig::activationDurationMs) {
        enterMode(AppMode::Listening, nowMs);
      }
      break;

    case AppMode::Listening:
      if (activationButtonReleased_ && pressed &&
          nowMs - buttonPressedAtMs_ >= AppConfig::listeningHoldToDeactivateMs) {
        enterMode(AppMode::Deactivation, nowMs);
      }
      break;

    case AppMode::Deactivation:
      if (nowMs - modeStartedAtMs_ >= AppConfig::deactivationDurationMs) {
        sessionCompletedThisBoot_ = true;
        enterMode(restingMode(), nowMs);
      }
      break;
  }

  previousButtonPressed_ = pressed;
}

uint8_t YapplApp::ledBrightnessFor(uint32_t nowMs, const AppState &snapshot) const {
  const uint32_t elapsed = nowMs - modeStartedAtMs_;

  switch (mode_) {
    case AppMode::Reminder:
      if (snapshot.lightLevel >= AppConfig::roomLightOffBelowPercent) {
        return cosineBreath(elapsed, AppConfig::reminderLightOnBreathMs, AppConfig::reminderLedMaxBrightness);
      }

      {
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
      return cosineBreath(elapsed, 500, AppConfig::reminderLedMaxBrightness);

    case AppMode::Listening:
      return AppConfig::listeningLedBrightness;

    case AppMode::Deactivation:
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
  const uint32_t elapsed = nowMs - modeStartedAtMs_;
  const uint16_t *melody = nullptr;
  size_t noteCount = 0;
  uint32_t noteMs = 0;

  if (mode_ == AppMode::Activation) {
    melody = kActivationMelodyHz;
    noteCount = sizeof(kActivationMelodyHz) / sizeof(kActivationMelodyHz[0]);
    noteMs = kActivationNoteMs;
  } else if (mode_ == AppMode::Deactivation) {
    melody = kDeactivationMelodyHz;
    noteCount = sizeof(kDeactivationMelodyHz) / sizeof(kDeactivationMelodyHz[0]);
    noteMs = kDeactivationNoteMs;
  } else {
    return 0;
  }

  const size_t index = elapsed / noteMs;
  if (index >= noteCount) {
    return 0;
  }
  return melody[index];
}

void YapplApp::setLedBrightness(uint8_t brightness) {
  if (!AppConfig::enableLed || brightness == lastLedBrightness_) {
    return;
  }

  led_.setBrightness(brightness);
  lastLedBrightness_ = brightness;
}

void YapplApp::setPiezoFrequency(uint16_t frequencyHz) {
  if (!AppConfig::enablePiezo || frequencyHz == currentPiezoFrequencyHz_) {
    return;
  }

  if (frequencyHz == 0) {
    piezo_.stop();
  } else {
    piezo_.play(frequencyHz);
  }
  currentPiezoFrequencyHz_ = frequencyHz;
}

void YapplApp::allocateRecordingBuffer() {
  if (recordingBuffer_ != nullptr || AppConfig::recordingBufferBytes == 0) {
    return;
  }

  recordingBuffer_ = static_cast<int32_t *>(
      heap_caps_malloc(AppConfig::recordingBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (recordingBuffer_ == nullptr) {
    Serial.println(F("PSRAM recording buffer unavailable"));
    recordingCapacitySamples_ = 0;
    return;
  }

  recordingCapacitySamples_ = AppConfig::recordingBufferBytes / sizeof(recordingBuffer_[0]);
  Serial.printf("PSRAM recording buffer ready: %u bytes\n", static_cast<unsigned>(AppConfig::recordingBufferBytes));
}

void YapplApp::resetRecording() {
  recordedSamples_ = 0;
}

void YapplApp::appendRecordingSamples(const int32_t *samples, size_t sampleCount) {
  if (recordingBuffer_ == nullptr || samples == nullptr || sampleCount == 0) {
    return;
  }

  const size_t available = recordingCapacitySamples_ - std::min(recordedSamples_, recordingCapacitySamples_);
  const size_t toCopy = std::min(sampleCount, available);
  if (toCopy == 0) {
    return;
  }

  memcpy(recordingBuffer_ + recordedSamples_, samples, toCopy * sizeof(samples[0]));
  recordedSamples_ += toCopy;
}

uint8_t YapplApp::micLevelFromSamples(const int32_t *samples, size_t sampleCount) const {
  if (samples == nullptr || sampleCount == 0) {
    return 0;
  }

  int32_t peakVolume = 0;
  for (size_t i = 0; i < sampleCount; ++i) {
    const int32_t sample = samples[i] >> kInmp441SlotShift;
    const int32_t volume = sample < 0 ? -sample : sample;
    peakVolume = std::max(peakVolume, volume);
  }

  int64_t meterValue = static_cast<int64_t>(peakVolume) - AppConfig::noiseFloor;
  meterValue = std::max<int64_t>(0, meterValue);
  meterValue = std::min<int64_t>(AppConfig::noiseCeiling, meterValue);
  return static_cast<uint8_t>(meterValue * 100 / AppConfig::noiseCeiling);
}

AppState YapplApp::stateSnapshot() {
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
  TickType_t lastWake = xTaskGetTickCount();

  while (true) {
    const uint32_t nowMs = millis();
    const AppState snapshot = stateSnapshot();

    updateMode(nowMs, snapshot);

    const uint8_t ledBrightness = ledBrightnessFor(nowMs, snapshot);
    const uint16_t piezoFrequencyHz = piezoFrequencyFor(nowMs);
    setLedBrightness(ledBrightness);
    setPiezoFrequency(piezoFrequencyHz);
    publishOutputState(mode_, ledBrightness, piezoFrequencyHz, recordedSamples_ * sizeof(recordingBuffer_[0]));

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(AppConfig::outputTaskPeriodMs));
  }
}

void YapplApp::sensorTask() {
  TickType_t lastWake = xTaskGetTickCount();
  uint32_t lastLogMs = 0;

  while (true) {
    const bool buttonPressed = AppConfig::enableButton && button_.isPressed();
    const int lightRaw = AppConfig::enablePhotoresistor ? photoresistor_.readRaw() : 0;
    const uint8_t lightLevel = lightLevelFromRaw(lightRaw);
    uint8_t micLevel = 0;

    if (micReady_) {
      const size_t samplesRead = mic_.read(micSamples_, AppConfig::micSampleCount);
      if (samplesRead > 0) {
        micLevel = micLevelFromSamples(micSamples_, samplesRead);
        if (stateSnapshot().mode == AppMode::Listening) {
          appendRecordingSamples(micSamples_, samplesRead);
        }
      }
    }

    publishSensorState(buttonPressed, lightRaw, lightLevel, micLevel);

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
  TickType_t lastWake = xTaskGetTickCount();

  while (true) {
    if (displayReady_) {
      const AppState snapshot = stateSnapshot();
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
