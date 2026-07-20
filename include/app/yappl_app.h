#pragma once

#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "app/app_state.h"
#include "app/config.h"
#include "app/state_controller.h"
#include "drivers/button.h"
#include "drivers/inmp441_microphone.h"
#include "drivers/oled_display.h"
#include "drivers/photoresistor.h"
#include "drivers/piezo_buzzer.h"
#include "drivers/status_led.h"
#include "network/backend_client.h"
#include "network/time_sync.h"
#include "network/wifi_manager.h"
#include "services/act_player.h"

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
  WifiManager wifi_;
  TimeSync timeSync_;
  BackendClient backend_;

  // Behavior service used by the display task.
  ActPlayer actPlayer_;
  StateController stateController_;

  // Scratch buffer for mic reads. Kept as a member so it does not consume task
  // stack every time the sensor task runs.
  int32_t micSamples_[AppConfig::micSampleCount] = {};
  uint8_t *audioRingBuffer_ = nullptr;
  uint8_t *audioUploadBatch_ = nullptr;
  SemaphoreHandle_t audioMutex_ = nullptr;
  size_t audioRingWriteIndex_ = 0;
  size_t audioRingReadIndex_ = 0;
  size_t audioRingBytesUsed_ = 0;
  std::atomic<size_t> recordedBytes_{0};
  std::atomic<size_t> droppedAudioBytes_{0};
  uint32_t nextAudioSequence_ = 1;
  portMUX_TYPE yapEpochMux_ = portMUX_INITIALIZER_UNLOCKED;
  portMUX_TYPE backendStateMux_ = portMUX_INITIALIZER_UNLOCKED;
  uint64_t lastYapEpochThisBoot_ = 0;
  bool pendingBackendModeKnown_ = false;
  AppMode pendingBackendMode_ = AppMode::IdleDay;

  // Shared state and RTOS handles.
  AppState state_;
  SemaphoreHandle_t stateMutex_ = nullptr;
  TaskHandle_t outputTaskHandle_ = nullptr;
  TaskHandle_t sensorTaskHandle_ = nullptr;
  TaskHandle_t displayTaskHandle_ = nullptr;
  TaskHandle_t networkTaskHandle_ = nullptr;
  bool displayReady_ = false;
  bool micReady_ = false;
  bool backendReady_ = false;
  std::atomic<bool> backendConnected_{false};
  std::atomic<bool> audioUploading_{false};
  bool tasksStarted_ = false;
  std::atomic<bool> pendingBackendYapCompleted_{false};
  std::atomic<uint64_t> pendingBackendYapCompletedEpoch_{0};
  std::atomic<bool> pendingBackendSessionStart_{false};
  std::atomic<bool> pendingBackendSessionFinish_{false};
  std::atomic<bool> audioCaptureActive_{false};
  String activeBackendSessionId_;
  uint8_t lastLedBrightness_ = 0;
  uint16_t currentPiezoFrequencyHz_ = 0;

  // Helpers for turning raw hardware inputs into product values.
  uint8_t lightLevelFromRaw(int raw) const;
  bool startTasks();
  bool validateHardware();
  TimeContext currentTimeContext();
  void rememberLastYapEpoch(uint64_t epoch);
  void setLastYapEpochFromBackend(uint64_t epoch);
  uint64_t lastYapEpochThisBoot();
  bool appModeFromBackendName(const String &name, AppMode &mode) const;
  void applyBackendStatus(const BackendStatus &status);
  bool consumePendingBackendMode(AppMode &mode);

  // Output helpers only touch hardware when the value actually changes.
  void setLedBrightness(uint8_t brightness);
  void setPiezoFrequency(uint16_t frequencyHz);

  // Backend audio streaming support. The sensor task writes converted PCM into
  // a PSRAM rolling buffer; the network task uploads 8 KB batches.
  void allocateAudioBuffers();
  void resetAudioStream();
  void pushAudioSamples(const int32_t *samples, size_t sampleCount);
  size_t peekAudioBatch(uint8_t *destination, size_t maxBytes);
  void commitAudioBatch(size_t byteCount);
  uint8_t micLevelFromSamples(const int32_t *samples, size_t sampleCount) const;

  // State helpers always take/release the mutex internally. Callers should copy
  // state quickly and then do slow hardware work outside the lock.
  AppState stateSnapshot();
  void publishSensorState(bool buttonPressed, int lightRaw, uint8_t lightLevel, uint8_t micLevel);
  void publishOutputState(AppMode mode,
                          bool wifiConnected,
                          bool backendConnected,
                          bool audioUploading,
                          bool timeSynced,
                          uint8_t currentHour,
                          uint8_t currentMinute,
                          uint8_t ledBrightness,
                          uint16_t piezoFrequencyHz,
                          size_t recordedBytes);

  // RTOS task bodies.
  void outputTask();
  void sensorTask();
  void displayTask();
  void networkTask();

  // FreeRTOS requires C-style entry points, so these thin wrappers cast the
  // context pointer back to the app instance.
  static void outputTaskEntry(void *context);
  static void sensorTaskEntry(void *context);
  static void displayTaskEntry(void *context);
  static void networkTaskEntry(void *context);
};

}  // namespace yappl
