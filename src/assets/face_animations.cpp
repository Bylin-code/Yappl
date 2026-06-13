#include "assets/face_animations.h"

namespace yappl {
namespace {

const FaceFrame kIdleStraightFrames[] = {
    {FaceBitmapId::EyesStraight, 1000, 0, 0, false},
};

const FaceFrame kBlinkFrames[] = {
    {FaceBitmapId::EyesStraight, 80, 0, 0, false},
    {FaceBitmapId::EyesBlinkHalf, 70, 0, 0, false},
    {FaceBitmapId::EyesBlinkClosed, 90, 0, 0, false},
    {FaceBitmapId::EyesBlinkHalf, 70, 0, 0, false},
    {FaceBitmapId::EyesStraight, 120, 0, 0, false},
};

const FaceFrame kLookLeftRightFrames[] = {
    {FaceBitmapId::EyesStraight, 150, 0, 0, false},
    {FaceBitmapId::EyesLookLeft, 550, 0, 0, false},
    {FaceBitmapId::EyesStraight, 180, 0, 0, false},
    {FaceBitmapId::EyesLookRight, 550, 0, 0, false},
    {FaceBitmapId::EyesStraight, 220, 0, 0, false},
};

const FaceFrame kReminderAnxiousFrames[] = {
    {FaceBitmapId::EyesAnxiousLeft, 450, 0, 0, false},
    {FaceBitmapId::EyesAnxiousRight, 450, 0, 0, false},
};

const FaceFrame kReminderShakeFrames[] = {
    {FaceBitmapId::EyesAnxiousLeft, 70, -2, 0, false},
    {FaceBitmapId::EyesAnxiousRight, 70, 2, 0, false},
    {FaceBitmapId::EyesAnxiousLeft, 70, -2, 0, false},
    {FaceBitmapId::EyesAnxiousRight, 70, 2, 0, false},
};

const FaceFrame kNotYetHeadShakeFrames[] = {
    {FaceBitmapId::EyesNotYetLeft, 120, -3, 0, false},
    {FaceBitmapId::EyesNotYetRight, 120, 3, 0, false},
    {FaceBitmapId::EyesNotYetLeft, 120, -3, 0, false},
    {FaceBitmapId::EyesNotYetRight, 120, 3, 0, false},
};

const FaceFrame kActivationDanceFrames[] = {
    {FaceBitmapId::EyesDanceUp, 120, 0, -2, false},
    {FaceBitmapId::EyesDanceRight, 120, 2, 0, false},
    {FaceBitmapId::EyesDanceDown, 120, 0, 2, false},
    {FaceBitmapId::EyesDanceLeft, 120, -2, 0, false},
};

const FaceFrame kListeningStraightFrames[] = {
    {FaceBitmapId::EyesStraight, 1000, 0, 0, false},
};

const FaceFrame kListeningNodFrames[] = {
    {FaceBitmapId::EyesNodUp, 140, 0, -2, false},
    {FaceBitmapId::EyesStraight, 100, 0, 0, false},
    {FaceBitmapId::EyesNodDown, 140, 0, 2, false},
    {FaceBitmapId::EyesStraight, 160, 0, 0, false},
};

const FaceFrame kDeactivationSleepFrames[] = {
    {FaceBitmapId::EyesStraight, 300, 0, 0, false},
    {FaceBitmapId::EyesSleepy1, 450, 0, 0, false},
    {FaceBitmapId::EyesSleepy2, 450, 0, 0, false},
    {FaceBitmapId::EyesSleepClosed, 500, 0, 0, false},
    {FaceBitmapId::EyesGoodNight, 250, 0, 0, false},
    {FaceBitmapId::EyesSleepClosed, 180, 0, 0, false},
    {FaceBitmapId::EyesGoodNight, 250, 0, 0, false},
    {FaceBitmapId::EyesSleepClosed, 180, 0, 0, false},
    {FaceBitmapId::EyesGoodNight, 250, 0, 0, false},
};

const FaceFrame kIdleNightSleepFrames[] = {
    {FaceBitmapId::EyesSleepClosed, 600, 0, 0, false},
    {FaceBitmapId::EyesNightZ1, 500, 0, 0, false},
    {FaceBitmapId::EyesNightZ2, 500, 0, 0, false},
    {FaceBitmapId::EyesNightZ3, 800, 0, 0, false},
};

const FaceAct kFaceActs[] = {
    {"idle_straight", kIdleStraightFrames, 1, true, 0, 0},
    {"blink", kBlinkFrames, 5, false, 2500, 18},
    {"look_left_right", kLookLeftRightFrames, 5, false, 5000, 8},
    {"reminder_anxious", kReminderAnxiousFrames, 2, true, 0, 0},
    {"reminder_shake", kReminderShakeFrames, 4, false, 1500, 35},
    {"not_yet_headshake", kNotYetHeadShakeFrames, 4, false, 0, 0},
    {"activation_dance", kActivationDanceFrames, 4, true, 0, 0},
    {"listening_straight", kListeningStraightFrames, 1, true, 0, 0},
    {"listening_nod", kListeningNodFrames, 4, false, 2500, 16},
    {"deactivation_sleep", kDeactivationSleepFrames, 9, false, 0, 0},
    {"idle_night_sleep", kIdleNightSleepFrames, 4, true, 0, 0},
};

}  // namespace

const FaceAct &faceAct(FaceActId id) {
  const uint8_t index = static_cast<uint8_t>(id);
  return kFaceActs[index];
}

}  // namespace yappl
