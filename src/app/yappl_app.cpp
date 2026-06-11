#include "app/yappl_app.h"

#include "app/config.h"

namespace yappl {

void YapplApp::begin() {
#ifdef YAPPL_DEBUG_SERIAL_BAUD
  Serial.begin(YAPPL_DEBUG_SERIAL_BAUD);
#else
  Serial.begin(115200);
#endif
  delay(1200);
  Serial.println();
  Serial.println(F("Yappl starting"));

#ifdef YAPPL_MIC_RAW_STREAM
  Serial.println(F("Mode: raw INMP441 serial stream"));
  Serial.println(F("CSV format: block_ms,sample_index,raw_i32,shifted_i24"));
  micReady_ = mic_.begin(AppConfig::sampleRateHz);
  Serial.println(micReady_ ? F("INMP441 ready") : F("INMP441 failed"));
  Serial.flush();
  return;
#endif

  displayReady_ = display_.begin();
  if (displayReady_) {
    display_.drawStatus(F("Yappl"), F("Starting mic..."));
  }

  micReady_ = mic_.begin(AppConfig::sampleRateHz);
  Serial.println(micReady_ ? F("INMP441 ready") : F("INMP441 failed"));

  if (displayReady_) {
    display_.drawStatus(F("Yappl"), F("Starting amp..."));
  }

  // Initialize the amp so app code can play audio later. The main loop does
  // not route mic audio to the speaker yet; call amp_.sanityCheck() here only
  // when you intentionally want the startup test tone.
  const bool ampReady = amp_.begin(AppConfig::sampleRateHz);
  Serial.println(ampReady ? F("MAX98357A ready") : F("MAX98357A failed"));

  if (displayReady_) {
    if (micReady_) {
      display_.drawStatus(F("Yappl"), F("Mic meter ready"));
    } else {
      display_.drawStatus(F("Mic error"), F("Check INMP441"));
    }
    delay(600);
    if (micReady_) {
      display_.drawMeter(0);
    }
  }

  if (micReady_) {
    mic_.sanityCheck();
  }
}

void YapplApp::update() {
#ifdef YAPPL_MIC_RAW_STREAM
  streamRawMicBlock();
  return;
#endif

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
    if (displayReady_) {
      display_.drawStatus(F("Mic read failed"), F("Check wiring"));
    }
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

#ifdef YAPPL_MIC_RAW_STREAM
void YapplApp::streamRawMicBlock() {
  if (!micReady_) {
    delay(250);
    Serial.println(F("Mic not ready"));
    return;
  }

  const size_t samplesRead = mic_.read(micSamples_, AppConfig::micSampleCount);
  if (samplesRead == 0) {
    delay(50);
    Serial.println(F("Mic read failed"));
    return;
  }

  const uint32_t blockMs = millis();
  constexpr size_t kDebugDecimation = 4;
  for (size_t i = 0; i < samplesRead; i += kDebugDecimation) {
    // INMP441 data is 24-bit audio carried in a 32-bit slot. Printing both
    // values makes it easy to see whether the ESP32 I2S packing needs shifting.
    const int32_t raw = micSamples_[i];
    const int32_t shifted = raw >> 8;
    Serial.printf("%lu,%u,%ld,%ld\n",
                  static_cast<unsigned long>(blockMs),
                  static_cast<unsigned>(i),
                  static_cast<long>(raw),
                  static_cast<long>(shifted));
  }

  Serial.flush();
  delay(50);
}
#endif

}  // namespace yappl
