#include "services/piezo_scale_player.h"

#include "app/config.h"

namespace yappl {
namespace {

// C major up to the octave and back down. C5 is included explicitly so the
// scale reaches the octave above the first note.
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
    // Reset to the first note every time the button is released.
    if (active_) {
      piezo_.stop();
    }
    active_ = false;
    noteIndex_ = 0;
    currentFrequencyHz_ = 0;
    return;
  }

  if (!active_) {
    // Start immediately on press instead of waiting one note interval.
    active_ = true;
    noteIndex_ = 0;
    lastNoteMs_ = nowMs;
    playCurrentNote();
    return;
  }

  if (nowMs - lastNoteMs_ < AppConfig::piezoNoteDurationMs) {
    return;
  }

  // Add the fixed period instead of setting lastNoteMs_ = nowMs. That preserves
  // a steadier musical clock if one update runs slightly late.
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
