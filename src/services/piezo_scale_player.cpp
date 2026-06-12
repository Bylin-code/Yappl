#include "services/piezo_scale_player.h"

#include "app/config.h"

namespace yappl {
namespace {

constexpr uint16_t kScaleHz[] = {
    262,  // C4
    294,  // D4
    330,  // E4
    349,  // F4
    392,  // G4
    440,  // A4
    494,  // B4
    523,  // C5
    494,  // B4
    440,  // A4
    392,  // G4
    349,  // F4
    330,  // E4
    294,  // D4
};

constexpr uint8_t kScaleNoteCount = sizeof(kScaleHz) / sizeof(kScaleHz[0]);

}  // namespace

PiezoScalePlayer::PiezoScalePlayer(PiezoBuzzer &piezo) : piezo_(piezo) {}

void PiezoScalePlayer::update(uint32_t nowMs, bool active) {
  if (!active) {
    if (active_) {
      piezo_.stop();
    }
    active_ = false;
    noteIndex_ = 0;
    currentFrequencyHz_ = 0;
    return;
  }

  if (!active_) {
    active_ = true;
    noteIndex_ = 0;
    lastNoteMs_ = nowMs;
    playCurrentNote();
    return;
  }

  if (nowMs - lastNoteMs_ < AppConfig::piezoNoteDurationMs) {
    return;
  }

  lastNoteMs_ += AppConfig::piezoNoteDurationMs;
  noteIndex_ = static_cast<uint8_t>((noteIndex_ + 1) % kScaleNoteCount);
  playCurrentNote();
}

uint16_t PiezoScalePlayer::currentFrequencyHz() const {
  return currentFrequencyHz_;
}

void PiezoScalePlayer::playCurrentNote() {
  currentFrequencyHz_ = kScaleHz[noteIndex_];
  piezo_.play(currentFrequencyHz_);
}

}  // namespace yappl
