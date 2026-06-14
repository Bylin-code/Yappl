#include "services/act_player.h"

namespace yappl {
namespace {

// Enums are stored as compact uint8_t values, so this helper makes array
// indexing explicit and readable.
constexpr uint8_t actIndex(FaceActId id) {
  return static_cast<uint8_t>(id);
}

// Return true approximately `percent` percent of the time. Used to decide
// whether optional acts like blinking should interrupt the default act.
bool randomChance(uint8_t percent) {
  return percent > 0 && random(100) < percent;
}

}  // namespace

const FaceFrame &ActPlayer::update(AppMode mode, uint32_t nowMs) {
  // A mode change always restarts the mode's default OLED act.
  if (mode != mode_) {
    mode_ = mode;
    restart(defaultActFor(mode_), nowMs);
  }

  const FaceAct &act = faceAct(currentActId_);
  const FaceFrame &frame = act.frames[frameIndex_];
  // If this frame has been held long enough, advance to the next frame.
  if (nowMs - frameStartedMs_ >= frame.durationMs) {
    frameStartedMs_ = nowMs;
    if (frameIndex_ + 1 < act.frameCount) {
      ++frameIndex_;
    } else if (act.loop) {
      frameIndex_ = 0;
    } else {
      // One-shot act finished: remember its cooldown start time, then fall back
      // to the state's default looping act.
      lastOptionalActMs_[actIndex(currentActId_)] = nowMs;
      restart(defaultActFor(mode_), nowMs);
    }
  }

  // Optional acts are only considered after normal frame advancement, so a
  // one-shot act can finish cleanly before another starts.
  maybeStartOptionalAct(mode_, nowMs);
  return faceAct(currentActId_).frames[frameIndex_];
}

FaceActId ActPlayer::defaultActFor(AppMode mode) const {
  // Default acts are the "normal face" for each state.
  switch (mode) {
    case AppMode::IdleDay:
      return FaceActId::IdleStraight;
    case AppMode::Reminder:
      return FaceActId::ReminderAnxious;
    case AppMode::NotYet:
      return FaceActId::NotYetHeadShake;
    case AppMode::Activation:
      return FaceActId::ActivationDance;
    case AppMode::Listening:
      return FaceActId::ListeningStraight;
    case AppMode::Deactivation:
      return FaceActId::DeactivationSleep;
    case AppMode::IdleNight:
      return FaceActId::IdleNightSleep;
  }
  return FaceActId::IdleStraight;
}

void ActPlayer::restart(FaceActId actId, uint32_t nowMs) {
  // Start the requested act at frame zero and begin timing from now.
  currentActId_ = actId;
  frameIndex_ = 0;
  frameStartedMs_ = nowMs;
}

void ActPlayer::maybeStartOptionalAct(AppMode mode, uint32_t nowMs) {
  const FaceActId defaultAct = defaultActFor(mode);
  // Optional acts can only interrupt the default act, not another optional act.
  if (currentActId_ != defaultAct) {
    return;
  }

  // Candidate optional acts. The allowed check below filters by current mode.
  const FaceActId options[] = {
      FaceActId::Blink,
      FaceActId::LookLeftRight,
      FaceActId::ReminderShake,
      FaceActId::ListeningNod,
  };

  for (const FaceActId option : options) {
    // This table is the state-to-optional-act policy.
    const bool allowed =
        (mode == AppMode::IdleDay && (option == FaceActId::Blink || option == FaceActId::LookLeftRight)) ||
        (mode == AppMode::Listening && (option == FaceActId::Blink || option == FaceActId::ListeningNod)) ||
        (mode == AppMode::Reminder && option == FaceActId::ReminderShake);
    if (!allowed) {
      continue;
    }

    const FaceAct &act = faceAct(option);
    const uint8_t index = actIndex(option);
    // Cooldown first, then random chance. This keeps common acts from firing
    // continuously every display tick.
    if (nowMs - lastOptionalActMs_[index] >= act.minDowntimeMs && randomChance(act.chancePercent)) {
      restart(option, nowMs);
      return;
    }
  }
}

}  // namespace yappl
