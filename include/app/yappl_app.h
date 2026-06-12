#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "app/app_state.h"
#include "drivers/button.h"
#include "drivers/inmp441_microphone.h"
#include "drivers/oled_display.h"
#include "drivers/photoresistor.h"
#include "drivers/piezo_buzzer.h"
#include "drivers/status_led.h"
#include "services/led_breather.h"
#include "services/piezo_scale_player.h"

namespace yappl {

// Top-level coordinator. It initializes hardware, owns the shared AppState, and
// starts the RTOS tasks. Hardware details live in drivers; behavior details live
// in services.
class YapplApp {
 public:
  void begin();
  void update();

 private:
  // Hardware drivers. Each driver is owned by exactly one app instance.
  OledDisplay display_;
  Inmp441Microphone mic_;
  StatusLed led_;
  PiezoBuzzer piezo_;
  Photoresistor photoresistor_;
  Button button_;

  // Behavior services used by the output task.
  LedBreather ledBreather_{led_};
  PiezoScalePlayer scalePlayer_{piezo_};

  // Scratch buffer for mic reads. Kept as a member so it does not consume task
  // stack every time the sensor task runs.
  int32_t micSamples_[256] = {};

  // Shared state and RTOS handles.
  AppState state_;
  SemaphoreHandle_t stateMutex_ = nullptr;
  TaskHandle_t outputTaskHandle_ = nullptr;
  TaskHandle_t sensorTaskHandle_ = nullptr;
  TaskHandle_t displayTaskHandle_ = nullptr;
  bool displayReady_ = false;
  bool micReady_ = false;
  bool tasksStarted_ = false;

  uint8_t lightLevelFromRaw(int raw) const;
  bool startTasks();

  // State helpers always take/release the mutex internally. Callers should copy
  // state quickly and then do slow hardware work outside the lock.
  AppState stateSnapshot();
  void publishSensorState(bool buttonPressed, int lightRaw, uint8_t lightLevel, uint8_t micLevel);
  void publishOutputState(uint8_t ledBrightness, uint16_t piezoFrequencyHz);

  // RTOS task bodies.
  void outputTask();
  void sensorTask();
  void displayTask();

  // FreeRTOS requires C-style entry points, so these thin wrappers cast the
  // context pointer back to the app instance.
  static void outputTaskEntry(void *context);
  static void sensorTaskEntry(void *context);
  static void displayTaskEntry(void *context);
};

}  // namespace yappl
