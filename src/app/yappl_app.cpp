#include "app/yappl_app.h"

#include "app/config.h"

namespace yappl {

void YapplApp::begin() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println(F("Yappl starting"));

  stateMutex_ = xSemaphoreCreateMutex();
  if (stateMutex_ == nullptr) {
    Serial.println(F("Failed to create app state mutex"));
    return;
  }

  // Drivers are initialized once before tasks start. After this, each task owns
  // its own timing and only shares data through AppState.
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

  // Publish an initial state so the display task has valid values immediately.
  const int lightRaw = AppConfig::enablePhotoresistor ? photoresistor_.readRaw() : 0;
  publishSensorState(false, lightRaw, lightLevelFromRaw(lightRaw), 0);

  Serial.println(micReady_ ? F("INMP441 ready") : F("INMP441 failed"));
  if (!startTasks()) {
    Serial.println(F("Failed to start RTOS tasks"));
  }
}

void YapplApp::update() {
  // Runtime behavior lives in FreeRTOS tasks. Keep Arduino's loop idle.
  delay(1000);
}

uint8_t YapplApp::lightLevelFromRaw(int raw) const {
  const int dark = AppConfig::photoresistorDarkRaw;
  const int bright = AppConfig::photoresistorBrightRaw;
  if (dark == bright) {
    return 0;
  }

  // This supports either normal or inverted calibration values, then clamps to
  // a display-friendly percentage.
  int32_t level = static_cast<int32_t>(raw - dark) * 100 / (bright - dark);
  level = constrain(level, 0, 100);
  return static_cast<uint8_t>(level);
}

bool YapplApp::startTasks() {
  if (tasksStarted_) {
    return true;
  }

  // Output has highest priority because it controls human-visible/audible
  // timing. Display has lowest priority because OLED I2C transfers are slow.
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

AppState YapplApp::stateSnapshot() {
  AppState snapshot;
  if (stateMutex_ == nullptr) {
    return snapshot;
  }

  // Copy under the mutex, then let callers do slow work without holding it.
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

void YapplApp::publishOutputState(uint8_t ledBrightness, uint16_t piezoFrequencyHz) {
  if (stateMutex_ == nullptr) {
    return;
  }

  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  state_.ledBrightness = ledBrightness;
  state_.piezoFrequencyHz = piezoFrequencyHz;
  xSemaphoreGive(stateMutex_);
}

void YapplApp::outputTask() {
  TickType_t lastWake = xTaskGetTickCount();

  while (true) {
    const uint32_t nowMs = millis();
    const AppState snapshot = stateSnapshot();

    // The output task reacts to the latest button state but owns LED/piezo
    // timing locally so slow display work cannot create jitter.
    if (AppConfig::enableLed) {
      ledBreather_.update(nowMs, snapshot.buttonPressed);
    }
    if (AppConfig::enablePiezo) {
      scalePlayer_.update(nowMs, snapshot.buttonPressed);
    }

    // Publish output state for the OLED and serial log.
    publishOutputState(AppConfig::enableLed ? ledBreather_.brightness() : 0,
                       AppConfig::enablePiezo ? scalePlayer_.currentFrequencyHz() : 0);

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(AppConfig::outputTaskPeriodMs));
  }
}

void YapplApp::sensorTask() {
  TickType_t lastWake = xTaskGetTickCount();
  uint32_t lastLogMs = 0;

  while (true) {
    // Sensor reads are grouped here because button/light/mic are inputs. Mic
    // reads can block for milliseconds, so this task stays below output
    // priority.
    const bool buttonPressed = AppConfig::enableButton && button_.isPressed();
    const int lightRaw = AppConfig::enablePhotoresistor ? photoresistor_.readRaw() : 0;
    const uint8_t lightLevel = lightLevelFromRaw(lightRaw);
    uint8_t micLevel = 0;

    if (micReady_) {
      MicLevelStats stats;
      if (mic_.readLevel(micSamples_, AppConfig::micSampleCount, stats)) {
        micLevel = stats.level;
      }
    }

    publishSensorState(buttonPressed, lightRaw, lightLevel, micLevel);

    // Serial logging belongs here so it never blocks the high-priority output
    // task. It logs a state snapshot, not raw task internals.
    const uint32_t nowMs = millis();
    if (AppConfig::enableSerialLog && nowMs - lastLogMs >= AppConfig::serialLogMs) {
      lastLogMs = nowMs;
      const AppState snapshot = stateSnapshot();
      Serial.printf("IO: button=%s light_raw=%d light=%u%% mic=%u%% led_pwm=%u piezo=%uHz\n",
                    snapshot.buttonPressed ? "pressed" : "open",
                    snapshot.lightRaw,
                    snapshot.lightLevel,
                    snapshot.micLevel,
                    snapshot.ledBrightness,
                    snapshot.piezoFrequencyHz);
    }

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(AppConfig::sensorTaskPeriodMs));
  }
}

void YapplApp::displayTask() {
  TickType_t lastWake = xTaskGetTickCount();

  while (true) {
    if (displayReady_) {
      // OLED drawing is intentionally isolated. It copies state first, then
      // performs the slow I2C transfer without holding the state mutex.
      const AppState snapshot = stateSnapshot();
      display_.drawHardwareStatus(snapshot.buttonPressed,
                                  snapshot.lightRaw,
                                  snapshot.lightLevel,
                                  snapshot.micLevel,
                                  snapshot.ledBrightness,
                                  snapshot.piezoFrequencyHz);
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
