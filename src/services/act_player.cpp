#include "services/act_player.h"

namespace yappl {
namespace {

constexpr uint8_t actIndex(FaceActId id) {
  return static_cast<uint8_t>(id);
}

bool randomChance(uint8_t percent) {
  return percent > 0 && random(100) < percent;
}

}  // namespace

const FaceFrame &ActPlayer::update(AppMode mode, uint32_t nowMs) {
  if (mode != mode_) {
    mode_ = mode;
    restart(defaultActFor(mode_), nowMs);
  }

  const FaceAct &act = faceAct(currentActId_);
  const FaceFrame &frame = act.frames[frameIndex_];
  if (nowMs - frameStartedMs_ >= frame.durationMs) {
    frameStartedMs_ = nowMs;
    if (frameIndex_ + 1 < act.frameCount) {
      ++frameIndex_;
    } else if (act.loop) {
      frameIndex_ = 0;
    } else {
      lastOptionalActMs_[actIndex(currentActId_)] = nowMs;
      restart(defaultActFor(mode_), nowMs);
    }
  }

  maybeStartOptionalAct(mode_, nowMs);
  return faceAct(currentActId_).frames[frameIndex_];
}

FaceActId ActPlayer::defaultActFor(AppMode mode) const {
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
  currentActId_ = actId;
  frameIndex_ = 0;
  frameStartedMs_ = nowMs;
}

void ActPlayer::maybeStartOptionalAct(AppMode mode, uint32_t nowMs) {
  const FaceActId defaultAct = defaultActFor(mode);
  if (currentActId_ != defaultAct) {
    return;
  }

  const FaceActId options[] = {
      FaceActId::Blink,
      FaceActId::LookLeftRight,
      FaceActId::ReminderShake,
      FaceActId::ListeningNod,
  };

  for (const FaceActId option : options) {
    const bool allowed =
        (mode == AppMode::IdleDay && (option == FaceActId::Blink || option == FaceActId::LookLeftRight)) ||
        (mode == AppMode::Listening && (option == FaceActId::Blink || option == FaceActId::ListeningNod)) ||
        (mode == AppMode::Reminder && option == FaceActId::ReminderShake);
    if (!allowed) {
      continue;
    }

    const FaceAct &act = faceAct(option);
    const uint8_t index = actIndex(option);
    if (nowMs - lastOptionalActMs_[index] >= act.minDowntimeMs && randomChance(act.chancePercent)) {
      restart(option, nowMs);
      return;
    }
  }
}

}  // namespace yappl
