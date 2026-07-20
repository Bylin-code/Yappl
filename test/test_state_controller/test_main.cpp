#include <Arduino.h>
#include <unity.h>

#include "app/config.h"
#include "app/state_controller.h"

using namespace yappl;

namespace {

TimeContext reminderTime() {
  TimeContext value;
  value.valid = true;
  value.hour = 21;
  value.minute = 0;
  value.nowEpoch = 200000;
  value.hasLastYap = false;
  return value;
}

void test_reminder_hold_starts_listening_and_release_guard_stops_it() {
  StateController controller;
  AppState state;
  const TimeContext time = reminderTime();
  controller.begin(1000, time);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AppMode::Reminder), static_cast<int>(controller.mode()));

  state.buttonPressed = true;
  controller.update(1100, state, time);
  controller.update(1100 + AppConfig::reminderHoldToActivateMs, state, time);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AppMode::Activation), static_cast<int>(controller.mode()));

  controller.update(1100 + AppConfig::reminderHoldToActivateMs + AppConfig::activationDurationMs,
                    state,
                    time);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AppMode::Listening), static_cast<int>(controller.mode()));

  // The activation press cannot immediately stop Listening.
  controller.update(5000, state, time);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AppMode::Listening), static_cast<int>(controller.mode()));
  state.buttonPressed = false;
  controller.update(5010, state, time);
  state.buttonPressed = true;
  controller.update(5020, state, time);
  controller.update(5020 + AppConfig::listeningHoldToDeactivateMs, state, time);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AppMode::Deactivation), static_cast<int>(controller.mode()));
  TEST_ASSERT_TRUE(controller.consumeSessionCompleted());
  TEST_ASSERT_FALSE(controller.consumeSessionCompleted());
}

void test_millis_wraparound_does_not_break_hold_duration() {
  StateController controller;
  AppState state;
  const TimeContext time = reminderTime();
  const uint32_t pressedAt = UINT32_MAX - 100;
  controller.begin(pressedAt, time);
  state.buttonPressed = true;
  controller.update(pressedAt, state, time);
  controller.update(pressedAt + AppConfig::reminderHoldToActivateMs, state, time);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AppMode::Activation), static_cast<int>(controller.mode()));
}

}  // namespace

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_reminder_hold_starts_listening_and_release_guard_stops_it);
  RUN_TEST(test_millis_wraparound_does_not_break_hold_duration);
  UNITY_END();
}

void loop() {}
