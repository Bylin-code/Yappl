#include "app/yappl_app.h"

#include "app/config.h"

namespace yappl {

void YapplApp::begin() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println(F("Yappl starting"));

  displayReady_ = display_.begin();
  micReady_ = mic_.begin(AppConfig::sampleRateHz);
  led_.begin();
  piezo_.begin();
  photoresistor_.begin();
  button_.begin();

  Serial.println(micReady_ ? F("INMP441 ready") : F("INMP441 failed"));

  lightRaw_ = photoresistor_.readRaw();
  lightLevel_ = lightLevelFromRaw(lightRaw_);

  if (displayReady_) {
    display_.drawHardwareStatus(buttonPressed_, lightRaw_, lightLevel_, micLevel_, ledBreather_.brightness());
  }
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

void YapplApp::update() {
  const uint32_t nowMs = millis();
  buttonPressed_ = button_.isPressed();

  ledBreather_.update(nowMs, buttonPressed_);
  scalePlayer_.update(nowMs, buttonPressed_);
  updateSensors(nowMs);
  updateDisplay(nowMs);
  updateSerialLog(nowMs);
}

void YapplApp::updateSensors(uint32_t nowMs) {
  if (nowMs - lastSensorMs_ < AppConfig::sensorUpdateMs) {
    return;
  }

  lastSensorMs_ = nowMs;
  lightRaw_ = photoresistor_.readRaw();
  lightLevel_ = lightLevelFromRaw(lightRaw_);

  if (!micReady_) {
    micLevel_ = 0;
    return;
  }

  MicLevelStats stats;
  if (mic_.readLevel(micSamples_, AppConfig::micSampleCount, stats)) {
    micLevel_ = stats.level;
  }
}

void YapplApp::updateDisplay(uint32_t nowMs) {
  if (!displayReady_ || nowMs - lastDisplayMs_ < AppConfig::displayUpdateMs) {
    return;
  }

  lastDisplayMs_ = nowMs;
  display_.drawHardwareStatus(buttonPressed_,
                              lightRaw_,
                              lightLevel_,
                              micLevel_,
                              ledBreather_.brightness());
}

void YapplApp::updateSerialLog(uint32_t nowMs) {
  if (nowMs - lastLogMs_ < AppConfig::serialLogMs) {
    return;
  }

  lastLogMs_ = nowMs;
  Serial.printf("IO: button=%s light_raw=%d light=%u%% mic=%u%% led_pwm=%u piezo=%uHz\n",
                buttonPressed_ ? "pressed" : "open",
                lightRaw_,
                lightLevel_,
                micLevel_,
                ledBreather_.brightness(),
                scalePlayer_.currentFrequencyHz());
}

}  // namespace yappl
