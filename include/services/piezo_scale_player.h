#pragma once

#include <Arduino.h>

#include "drivers/piezo_buzzer.h"

namespace yappl {

// Behavior service for the passive piezo. It plays a fixed up/down scale while
// active, using millis timestamps supplied by the output task.
class PiezoScalePlayer {
 public:
  explicit PiezoScalePlayer(PiezoBuzzer &piezo);

  void update(uint32_t nowMs, bool active);
  uint16_t currentFrequencyHz() const;

 private:
  PiezoBuzzer &piezo_;
  // Timing state for the current scale sequence.
  uint32_t lastNoteMs_ = 0;
  uint8_t noteIndex_ = 0;
  uint16_t currentFrequencyHz_ = 0;
  bool active_ = false;

  void playCurrentNote();
};

}  // namespace yappl
