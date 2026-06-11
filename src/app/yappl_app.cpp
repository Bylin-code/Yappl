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
  Serial.println(micReady_ ? F("INMP441 ready") : F("INMP441 failed"));

  // Initialize the amp so app code can play audio later. The main loop does
  // not route mic audio to the speaker yet.
  const bool ampReady = amp_.begin(AppConfig::sampleRateHz);
  Serial.println(ampReady ? F("MAX98357A ready") : F("MAX98357A failed"));

  if (displayReady_ && micReady_) {
    display_.drawMeter(0);
  }
}

void YapplApp::update() {
  const uint32_t nowMs = millis();
  if (nowMs - lastFrameMs_ < 50) {
    return;
  }
  lastFrameMs_ = nowMs;

  if (!micReady_) {
    return;
  }

  MicLevelStats stats;
  if (!mic_.readLevel(micSamples_, AppConfig::micSampleCount, stats)) {
    Serial.println(F("Mic read failed"));
    return;
  }

  if (nowMs - lastLogMs_ >= 500) {
    lastLogMs_ = nowMs;
    Serial.printf("OLED meter: min=%ld max=%ld volume=%ld level=%u%%\n",
                  static_cast<long>(stats.minimum),
                  static_cast<long>(stats.maximum),
                  static_cast<long>(stats.span),
                  stats.level);
  }

  if (displayReady_) {
    display_.drawMeter(stats.level);
  }
}

}  // namespace yappl
