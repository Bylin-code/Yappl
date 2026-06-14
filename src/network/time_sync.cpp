#include "network/time_sync.h"

#include <stdlib.h>
#include <time.h>

#include "app/config.h"

namespace yappl {

bool TimeSync::begin() {
  if (!AppConfig::enableTimeSync) {
    Serial.println(F("Time sync disabled in AppConfig"));
    return false;
  }

  Serial.println(F("Syncing time with NTP"));
  // Set TZ explicitly before and after configTzTime. Some Arduino-ESP32 builds
  // sync UTC correctly but do not keep the local timezone active unless TZ is
  // set in the process environment.
  setenv("TZ", AppConfig::timeZone, 1);
  tzset();

  // configTzTime asks NTP for UTC, stores it in the ESP32 system clock, and
  // configures localtime() to report Pacific time using the POSIX TZ string.
  configTzTime(AppConfig::timeZone, AppConfig::ntpServer1, AppConfig::ntpServer2);
  setenv("TZ", AppConfig::timeZone, 1);
  tzset();

  const uint32_t startedAtMs = millis();
  while (!isSynced() && millis() - startedAtMs < AppConfig::timeSyncTimeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (!isSynced()) {
    Serial.println(F("NTP time sync failed"));
    return false;
  }

  uint8_t hour = 0;
  uint8_t minute = 0;
  currentTime(hour, minute);
  Serial.printf("NTP time synced: %02u:%02u\n", hour, minute);
  return true;
}

bool TimeSync::isSynced() const {
  // Before NTP sync, ESP32 time is near Unix epoch. 2023+ is enough to prove
  // that a real network time value has been received.
  return time(nullptr) > 1672531200;
}

bool TimeSync::currentTime(uint8_t &hour, uint8_t &minute) const {
  if (!isSynced()) {
    return false;
  }

  const time_t now = time(nullptr);
  tm local = {};
  if (localtime_r(&now, &local) == nullptr) {
    return false;
  }

  hour = static_cast<uint8_t>(local.tm_hour);
  minute = static_cast<uint8_t>(local.tm_min);
  return true;
}

bool TimeSync::currentEpoch(uint64_t &epoch) const {
  if (!isSynced()) {
    return false;
  }

  epoch = static_cast<uint64_t>(time(nullptr));
  return true;
}

}  // namespace yappl
