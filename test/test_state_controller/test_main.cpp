#include <Arduino.h>
#include <unity.h>
#include <stdlib.h>

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

void test_stale_backend_reminder_yields_to_completed_session() {
  setenv("TZ", AppConfig::timeZone, 1);
  tzset();
  StateController controller;
  AppState state;
  TimeContext time = reminderTime();
  time.nowEpoch = 1789875799;  // September 19, 2026, after 8 PM Chicago.
  time.hour = 22;
  time.hasLastYap = true;
  time.lastYapEpoch = time.nowEpoch - 60;
  controller.begin(1000, time);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AppMode::IdleNight), static_cast<int>(controller.mode()));
  controller.applyBackendMode(AppMode::Reminder, 1100);
  controller.update(1105, state, time);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AppMode::IdleNight), static_cast<int>(controller.mode()));
}

void test_invalid_clock_recovers_to_reminder_then_returns_to_idle_if_invalid() {
  StateController controller;
  AppState state;
  TimeContext time = reminderTime();
  time.valid = false;
  controller.begin(1000, time);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AppMode::IdleDay), static_cast<int>(controller.mode()));
  time.valid = true;
  controller.update(1100, state, time);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AppMode::Reminder), static_cast<int>(controller.mode()));
  time.valid = false;
  controller.update(1200, state, time);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AppMode::IdleDay), static_cast<int>(controller.mode()));
}

void test_ready_requires_evening_and_more_than_twelve_hours() {
  struct Case {
    uint8_t hour;
    bool hasLastYap;
    int64_t elapsed;
    AppMode expected;
  };
  const Case cases[] = {
      {19, true, 86400, AppMode::IdleDay},
      {20, true, 43200, AppMode::IdleNight},
      {20, true, 43201, AppMode::Reminder},
      {23, true, 43201, AppMode::Reminder},
      {21, true, 43199, AppMode::IdleNight},
      {21, true, -60, AppMode::IdleNight},
      {0, true, 86400, AppMode::IdleNight},
      {8, false, 0, AppMode::IdleDay},
      {20, false, 0, AppMode::Reminder},
  };
  StateController controller;
  AppState state;
  uint32_t nowMs = 1000;
  for (const auto &value : cases) {
    TimeContext time = reminderTime();
    time.hour = value.hour;
    time.hasLastYap = value.hasLastYap;
    time.lastYapEpoch = static_cast<uint64_t>(static_cast<int64_t>(time.nowEpoch) - value.elapsed);
    // Verify both clock-driven transitions and the mode selected on boot.
    controller.update(nowMs++, state, time);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(value.expected), static_cast<int>(controller.mode()));
    controller.begin(nowMs++, time);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(value.expected), static_cast<int>(controller.mode()));
  }
}

}  // namespace

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_reminder_hold_starts_listening_and_release_guard_stops_it);
  RUN_TEST(test_millis_wraparound_does_not_break_hold_duration);
  RUN_TEST(test_stale_backend_reminder_yields_to_completed_session);
  RUN_TEST(test_invalid_clock_recovers_to_reminder_then_returns_to_idle_if_invalid);
  RUN_TEST(test_ready_requires_evening_and_more_than_twelve_hours);
  UNITY_END();
}

void loop() {}
