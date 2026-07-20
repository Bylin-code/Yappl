#include "app/yappl_app.h"

#include <algorithm>

#include <esp_heap_caps.h>
#include <esp_system.h>

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

  // Step one for internet support: connect to Wi-Fi once at boot. This is
  // intentionally before RTOS tasks so network bring-up cannot race the app.
  if (wifi_.begin()) {
    timeSync_.begin();
  }
  backendReady_ = backend_.begin();
  if (backendReady_ && wifi_.isConnected()) {
    const BackendStatus status = backend_.fetchStatus();
    backendConnected_ = status.requestOk;
    applyBackendStatus(status);
  }

  // The three RTOS tasks share AppState, so access is protected by this mutex.
  stateMutex_ = xSemaphoreCreateMutex();
  audioMutex_ = xSemaphoreCreateMutex();
  if (stateMutex_ == nullptr || audioMutex_ == nullptr) {
    Serial.println(F("Failed to create application mutexes"));
    return;
  }

  if (!validateHardware()) {
    // Keep the local UI alive even if the selected PlatformIO profile does not
    // perfectly match the connected module. Audio allocation below will fall
    // back to internal RAM and report failure independently if necessary.
    Serial.println(F("Hardware profile mismatch; continuing with available hardware"));
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

  allocateAudioBuffers();

  // Seed AppState with enough data for display/output tasks to start cleanly.
  const int lightRaw = AppConfig::enablePhotoresistor ? photoresistor_.readRaw() : 0;
  const TimeContext time = currentTimeContext();
  stateController_.begin(millis(), time);
  publishSensorState(false, lightRaw, lightLevelFromRaw(lightRaw), 0);
  publishOutputState(stateController_.mode(), wifi_.isConnected(), backendConnected_.load(), false, time.valid, time.hour, time.minute, 0, 0, 0);

  Serial.println(micReady_ ? F("INMP441 ready") : F("INMP441 failed"));
  Serial.printf("Clock %s %02u:%02u, last yap %s, boot mode %s\n",
                time.valid ? "valid" : "invalid",
                time.hour,
                time.minute,
                time.hasLastYap ? "cloud/RAM" : "none",
                StateController::modeName(stateController_.mode()));

  if (!startTasks()) {
    Serial.println(F("Failed to start RTOS tasks"));
  }
}

bool YapplApp::validateHardware() {
  const uint32_t flashBytes = ESP.getFlashChipSize();
  const bool psramReady = psramFound();
  const uint32_t psramBytes = ESP.getPsramSize();
  Serial.printf("Hardware: flash=%u MB psram=%s (%u MB) heap=%u bytes\n",
                static_cast<unsigned>(flashBytes / (1024 * 1024)),
                psramReady ? "ready" : "missing",
                static_cast<unsigned>(psramBytes / (1024 * 1024)),
                static_cast<unsigned>(ESP.getFreeHeap()));

  if (flashBytes < AppConfig::requiredFlashBytes) {
    Serial.printf("Expected at least %u MB flash\n",
                  static_cast<unsigned>(AppConfig::requiredFlashBytes / (1024 * 1024)));
    return false;
  }
  if (AppConfig::requirePsram && (!psramReady || psramBytes < AppConfig::requiredPsramBytes)) {
    Serial.printf("Expected at least %u MB PSRAM\n",
                  static_cast<unsigned>(AppConfig::requiredPsramBytes / (1024 * 1024)));
    return false;
  }
  return true;
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

  const BaseType_t networkStarted = xTaskCreate(
      networkTaskEntry,
      "yappl-network",
      AppConfig::networkTaskStackBytes,
      this,
      AppConfig::networkTaskPriority,
      &networkTaskHandle_);

  // If any task fails to start, do not pretend the RTOS app is running.
  tasksStarted_ = outputStarted == pdPASS && sensorStarted == pdPASS && displayStarted == pdPASS && networkStarted == pdPASS;
  return tasksStarted_;
}

TimeContext YapplApp::currentTimeContext() {
  TimeContext context;
  context.valid = timeSync_.currentTime(context.hour, context.minute) &&
                  timeSync_.currentEpoch(context.nowEpoch);

  // Cloud is the only persistent source for last-yap history. During one boot,
  // RAM remembers a just-completed or backend-fetched epoch so the state machine
  // can react immediately without writing journal history to ESP32 flash.
  context.lastYapEpoch = lastYapEpochThisBoot();
  context.hasLastYap = context.lastYapEpoch > 0;
  return context;
}

void YapplApp::rememberLastYapEpoch(uint64_t epoch) {
  if (epoch == 0) {
    return;
  }

  portENTER_CRITICAL(&yapEpochMux_);
  if (epoch > lastYapEpochThisBoot_) {
    lastYapEpochThisBoot_ = epoch;
  }
  portEXIT_CRITICAL(&yapEpochMux_);
}

void YapplApp::setLastYapEpochFromBackend(uint64_t epoch) {
  portENTER_CRITICAL(&yapEpochMux_);
  lastYapEpochThisBoot_ = epoch;
  portEXIT_CRITICAL(&yapEpochMux_);
}

uint64_t YapplApp::lastYapEpochThisBoot() {
  portENTER_CRITICAL(&yapEpochMux_);
  const uint64_t epoch = lastYapEpochThisBoot_;
  portEXIT_CRITICAL(&yapEpochMux_);
  return epoch;
}

bool YapplApp::appModeFromBackendName(const String &name, AppMode &mode) const {
  if (name == "idle_day") {
    mode = AppMode::IdleDay;
    return true;
  }
  if (name == "idle_night") {
    mode = AppMode::IdleNight;
    return true;
  }
  if (name == "reminder") {
    mode = AppMode::Reminder;
    return true;
  }
  return false;
}

void YapplApp::applyBackendStatus(const BackendStatus &status) {
  if (!status.requestOk) {
    return;
  }

  setLastYapEpochFromBackend(status.lastYapCompletedAtEpoch);

  AppMode backendMode = AppMode::IdleDay;
  if (appModeFromBackendName(status.mode, backendMode)) {
    portENTER_CRITICAL(&backendStateMux_);
    pendingBackendMode_ = backendMode;
    pendingBackendModeKnown_ = true;
    portEXIT_CRITICAL(&backendStateMux_);
  }
}

bool YapplApp::consumePendingBackendMode(AppMode &mode) {
  portENTER_CRITICAL(&backendStateMux_);
  const bool known = pendingBackendModeKnown_;
  if (known) {
    mode = pendingBackendMode_;
    pendingBackendModeKnown_ = false;
  }
  portEXIT_CRITICAL(&backendStateMux_);
  return known;
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

void YapplApp::allocateAudioBuffers() {
  if (audioRingBuffer_ != nullptr && audioUploadBatch_ != nullptr) {
    return;
  }

  audioRingBuffer_ = static_cast<uint8_t *>(
      heap_caps_malloc(AppConfig::audioRollingBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (audioRingBuffer_ == nullptr) {
    audioRingBuffer_ = static_cast<uint8_t *>(
        heap_caps_malloc(AppConfig::audioRollingBufferBytes, MALLOC_CAP_8BIT));
  }

  audioUploadBatch_ = static_cast<uint8_t *>(
      heap_caps_malloc(AppConfig::audioUploadBatchBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (audioUploadBatch_ == nullptr) {
    audioUploadBatch_ = static_cast<uint8_t *>(
        heap_caps_malloc(AppConfig::audioUploadBatchBytes, MALLOC_CAP_8BIT));
  }

  Serial.printf("Audio rolling buffer: %s (%u bytes), upload batch: %s (%u bytes)\n",
                audioRingBuffer_ != nullptr ? "ready" : "failed",
                static_cast<unsigned>(AppConfig::audioRollingBufferBytes),
                audioUploadBatch_ != nullptr ? "ready" : "failed",
                static_cast<unsigned>(AppConfig::audioUploadBatchBytes));
}

void YapplApp::resetAudioStream() {
  // A new Listening session must start with a clean backend lifecycle. If a
  // stale finish flag survives from a previous session, the next session can be
  // created and immediately finalized, producing a tiny MP3 at the start.
  pendingBackendSessionStart_ = false;
  pendingBackendSessionFinish_ = false;
  pendingBackendYapCompleted_ = false;
  pendingBackendYapCompletedEpoch_ = 0;
  audioCaptureActive_ = false;
  recordedBytes_ = 0;
  droppedAudioBytes_ = 0;
  xSemaphoreTake(audioMutex_, portMAX_DELAY);
  audioRingWriteIndex_ = 0;
  audioRingReadIndex_ = 0;
  audioRingBytesUsed_ = 0;
  xSemaphoreGive(audioMutex_);
}

void YapplApp::pushAudioSamples(const int32_t *samples, size_t sampleCount) {
  if (audioRingBuffer_ == nullptr || samples == nullptr || sampleCount == 0) {
    return;
  }

  int16_t pcmSamples[AppConfig::micSampleCount] = {};
  const size_t samplesToCopy = std::min(sampleCount, AppConfig::micSampleCount);
  for (size_t i = 0; i < samplesToCopy; ++i) {
    // INMP441 has 24 useful signed bits in a 32-bit slot. Shift into 24-bit
    // range, then down to signed 16-bit PCM for compact upload/storage. Apply a
    // small configurable gain because bedside speech is otherwise quiet.
    const int32_t sample24 = samples[i] >> kInmp441SlotShift;
    const int32_t sample16 = (sample24 >> 8) * AppConfig::audioUploadGain;
    pcmSamples[i] = static_cast<int16_t>(constrain(sample16, -32768, 32767));
  }

  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(pcmSamples);
  const size_t byteCount = samplesToCopy * sizeof(pcmSamples[0]);

  xSemaphoreTake(audioMutex_, portMAX_DELAY);
  for (size_t i = 0; i < byteCount; ++i) {
    if (audioRingBytesUsed_ == AppConfig::audioRollingBufferBytes) {
      // Preserve already-buffered (and possibly in-flight) audio. Overwriting
      // the oldest bytes could mutate a batch while HTTP is retrying it.
      droppedAudioBytes_.fetch_add(byteCount - i);
      break;
    }

    audioRingBuffer_[audioRingWriteIndex_] = bytes[i];
    audioRingWriteIndex_ = (audioRingWriteIndex_ + 1) % AppConfig::audioRollingBufferBytes;
    ++audioRingBytesUsed_;
  }
  recordedBytes_ += byteCount;
  xSemaphoreGive(audioMutex_);
}

size_t YapplApp::peekAudioBatch(uint8_t *destination, size_t maxBytes) {
  if (audioRingBuffer_ == nullptr || destination == nullptr || maxBytes == 0) {
    return 0;
  }

  xSemaphoreTake(audioMutex_, portMAX_DELAY);
  const size_t toRead = std::min(maxBytes, audioRingBytesUsed_);
  size_t readIndex = audioRingReadIndex_;
  for (size_t i = 0; i < toRead; ++i) {
    destination[i] = audioRingBuffer_[readIndex];
    readIndex = (readIndex + 1) % AppConfig::audioRollingBufferBytes;
  }
  xSemaphoreGive(audioMutex_);
  return toRead;
}

void YapplApp::commitAudioBatch(size_t byteCount) {
  xSemaphoreTake(audioMutex_, portMAX_DELAY);
  const size_t committed = std::min(byteCount, audioRingBytesUsed_);
  audioRingReadIndex_ = (audioRingReadIndex_ + committed) % AppConfig::audioRollingBufferBytes;
  audioRingBytesUsed_ -= committed;
  xSemaphoreGive(audioMutex_);
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
                                  bool wifiConnected,
                                  bool backendConnected,
                                  bool audioUploading,
                                  bool timeSynced,
                                  uint8_t currentHour,
                                  uint8_t currentMinute,
                                  uint8_t ledBrightness,
                                  uint16_t piezoFrequencyHz,
                                  size_t recordedBytes) {
  // Output task publishes product mode and hardware-output fields.
  if (stateMutex_ == nullptr) {
    return;
  }

  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  state_.mode = mode;
  state_.wifiConnected = wifiConnected;
  state_.backendConnected = backendConnected;
  state_.audioUploading = audioUploading;
  state_.timeSynced = timeSynced;
  state_.currentHour = currentHour;
  state_.currentMinute = currentMinute;
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
    // transition side effects it owns, such as starting/stopping audio upload.
    AppMode backendMode = AppMode::IdleDay;
    if (consumePendingBackendMode(backendMode) &&
        stateController_.applyBackendMode(backendMode, nowMs)) {
      Serial.printf("Mode synced from backend -> %s\n", StateController::modeName(backendMode));
    }

    TimeContext time = currentTimeContext();
    const bool modeChanged = stateController_.update(nowMs, snapshot, time);
    const AppMode mode = stateController_.mode();
    if (modeChanged) {
      if (mode == AppMode::Listening) {
        resetAudioStream();
        pendingBackendSessionStart_ = true;
        audioCaptureActive_ = true;
        Serial.println(F("Audio session capture armed"));
      }
      Serial.printf("Mode -> %s\n", StateController::modeName(mode));
    }

    if (stateController_.consumeClearYapRequested()) {
      portENTER_CRITICAL(&yapEpochMux_);
      lastYapEpochThisBoot_ = 0;
      portEXIT_CRITICAL(&yapEpochMux_);
      time = currentTimeContext();
    }

    if (stateController_.consumeSessionCompleted()) {
      pendingBackendYapCompleted_ = true;
      pendingBackendYapCompletedEpoch_ = time.valid ? time.nowEpoch : 0;
      pendingBackendSessionFinish_ = true;
      audioCaptureActive_ = false;
      size_t bufferedBytes = 0;
      xSemaphoreTake(audioMutex_, portMAX_DELAY);
      bufferedBytes = audioRingBytesUsed_;
      xSemaphoreGive(audioMutex_);
      Serial.printf("Audio session finish requested, buffered=%u dropped=%u\n",
                    static_cast<unsigned>(bufferedBytes),
                    static_cast<unsigned>(droppedAudioBytes_.load()));
      if (time.valid) {
        rememberLastYapEpoch(time.nowEpoch);
        time = currentTimeContext();
      } else {
        Serial.println(F("Session complete, but time is invalid; backend will receive epoch 0"));
      }
    }

    // Convert the current state into desired hardware outputs.
    const uint8_t ledBrightness =
        OutputPatterns::ledBrightnessFor(mode, stateController_.modeStartedAtMs(), nowMs, snapshot);
    const uint16_t piezoFrequencyHz =
        OutputPatterns::piezoFrequencyFor(mode, stateController_.modeStartedAtMs(), nowMs);
    setLedBrightness(ledBrightness);
    setPiezoFrequency(piezoFrequencyHz);

    publishOutputState(mode,
                       wifi_.isConnected(),
                       backendConnected_.load(),
                       audioUploading_.load(),
                       time.valid,
                       time.hour,
                       time.minute,
                       ledBrightness,
                       piezoFrequencyHz,
                       recordedBytes_.load());

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
      // samples are also queued for backend upload as 16-bit PCM.
      const size_t samplesRead = mic_.read(micSamples_, AppConfig::micSampleCount);
      if (samplesRead > 0) {
        micLevel = micLevelFromSamples(micSamples_, samplesRead);
        if (audioCaptureActive_.load()) {
          pushAudioSamples(micSamples_, samplesRead);
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
      display_.drawFaceFrame(frame,
                             snapshot.wifiConnected,
                             snapshot.backendConnected,
                             snapshot.audioUploading,
                             snapshot.timeSynced,
                             snapshot.currentHour,
                             snapshot.currentMinute);
    }

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(AppConfig::displayTaskPeriodMs));
  }
}

void YapplApp::networkTask() {
  // Lowest-priority app task. HTTP can block for seconds on a bad network, so
  // backend work lives here instead of inside output/display timing loops.
  TickType_t lastWake = xTaskGetTickCount();
  uint32_t lastPingMs = millis() - AppConfig::backendPingPeriodMs;
  uint32_t lastStatusMs = millis() - AppConfig::backendStatusPeriodMs;

  while (true) {
    const uint32_t nowMs = millis();
    bool connected = backendConnected_.load();

    if (backendReady_ && wifi_.isConnected()) {
      if (pendingBackendSessionStart_) {
        activeBackendSessionId_ = "";
        nextAudioSequence_ = 1;
        const TimeContext time = currentTimeContext();
        activeBackendSessionId_ = backend_.startAudioSession(time.valid ? time.nowEpoch : 0, AppConfig::sampleRateHz);
        pendingBackendSessionStart_ = activeBackendSessionId_.length() == 0;
        connected = activeBackendSessionId_.length() > 0;
        if (activeBackendSessionId_.length() > 0) {
          Serial.printf("Audio session started: %s\n", activeBackendSessionId_.c_str());
        }
      }

      uint8_t batchesUploadedThisPass = 0;
      while (activeBackendSessionId_.length() > 0 &&
             audioUploadBatch_ != nullptr &&
             batchesUploadedThisPass < AppConfig::audioUploadBatchesPerNetworkPass) {
        const size_t batchBytes = peekAudioBatch(audioUploadBatch_, AppConfig::audioUploadBatchBytes);
        if (batchBytes == 0) {
          break;
        }
        uint32_t acknowledgedSequence = 0;
        const bool uploaded = backend_.uploadAudioChunk(activeBackendSessionId_,
                                                        nextAudioSequence_,
                                                        audioUploadBatch_,
                                                        batchBytes,
                                                        acknowledgedSequence);
        if (uploaded && acknowledgedSequence == nextAudioSequence_) {
          commitAudioBatch(batchBytes);
          ++nextAudioSequence_;
          connected = true;
        } else {
          connected = false;
          break;
        }
        ++batchesUploadedThisPass;
      }

      size_t bufferedBytes = 0;
      xSemaphoreTake(audioMutex_, portMAX_DELAY);
      bufferedBytes = audioRingBytesUsed_;
      xSemaphoreGive(audioMutex_);
      audioUploading_.store(activeBackendSessionId_.length() > 0 &&
                            (audioCaptureActive_.load() || bufferedBytes > 0));
      if (pendingBackendSessionFinish_ && activeBackendSessionId_.length() > 0 && bufferedBytes == 0) {
        if (backend_.finishAudioSession(activeBackendSessionId_, pendingBackendYapCompletedEpoch_.load())) {
          Serial.printf("Audio session finished: %s bytes=%u dropped=%u\n",
                        activeBackendSessionId_.c_str(),
                        static_cast<unsigned>(recordedBytes_.load()),
                        static_cast<unsigned>(droppedAudioBytes_.load()));
          pendingBackendSessionFinish_ = false;
          activeBackendSessionId_ = "";
          audioUploading_.store(false);
          connected = true;
        } else {
          connected = false;
        }
      }

      if (pendingBackendYapCompleted_ && !pendingBackendSessionFinish_) {
        if (backend_.sendYapCompleted(pendingBackendYapCompletedEpoch_.load())) {
          pendingBackendYapCompleted_ = false;
          connected = true;
        } else {
          connected = false;
        }
      }

      if (nowMs - lastStatusMs >= AppConfig::backendStatusPeriodMs) {
        lastStatusMs = nowMs;
        const BackendStatus status = backend_.fetchStatus();
        connected = status.requestOk;
        applyBackendStatus(status);
      }

      if (nowMs - lastPingMs >= AppConfig::backendPingPeriodMs) {
        lastPingMs = nowMs;
        const AppState snapshot = stateSnapshot();
        const BackendStatus status = backend_.ping(snapshot.wifiConnected,
                                                   snapshot.timeSynced,
                                                   StateController::modeName(snapshot.mode));
        connected = status.requestOk;
        applyBackendStatus(status);
      }
    } else {
      connected = false;
      audioUploading_.store(false);
    }

    backendConnected_ = connected;
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(AppConfig::networkTaskPeriodMs));
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

void YapplApp::networkTaskEntry(void *context) {
  static_cast<YapplApp *>(context)->networkTask();
}

}  // namespace yappl
