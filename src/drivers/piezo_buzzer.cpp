#include "drivers/piezo_buzzer.h"

#include "app/config.h"

namespace yappl {

bool PiezoBuzzer::begin() {
  pinMode(AppConfig::piezoPin, OUTPUT);
  stop();
  started_ = true;
  return true;
}

void PiezoBuzzer::play(uint16_t frequencyHz) {
  if (!started_ || frequencyHz == 0) {
    return;
  }

  tone(AppConfig::piezoPin, frequencyHz);
}

void PiezoBuzzer::playFor(uint16_t frequencyHz, uint32_t durationMs) {
  if (!started_ || frequencyHz == 0 || durationMs == 0) {
    return;
  }

  tone(AppConfig::piezoPin, frequencyHz, durationMs);
}

void PiezoBuzzer::stop() {
  noTone(AppConfig::piezoPin);
  digitalWrite(AppConfig::piezoPin, LOW);
}

}  // namespace yappl
