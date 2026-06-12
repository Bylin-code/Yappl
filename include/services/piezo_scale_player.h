#pragma once

#include <Arduino.h>

#include "drivers/piezo_buzzer.h"

namespace yappl {

class PiezoScalePlayer {
 public:
  explicit PiezoScalePlayer(PiezoBuzzer &piezo);

  void update(uint32_t nowMs, bool active);
  uint16_t currentFrequencyHz() const;

 private:
  PiezoBuzzer &piezo_;
  uint32_t lastNoteMs_ = 0;
  uint8_t noteIndex_ = 0;
  uint16_t currentFrequencyHz_ = 0;
  bool active_ = false;

  void playCurrentNote();
};

}  // namespace yappl
