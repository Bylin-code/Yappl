#pragma once

#include <Arduino.h>

namespace yappl {

// Fetches real time from NTP after Wi-Fi is connected. Once synced, the ESP32
// keeps time locally and the display can read it without another network call.
class TimeSync {
 public:
  bool begin();
  bool isSynced() const;
  bool currentTime(uint8_t &hour, uint8_t &minute) const;
  bool currentEpoch(uint64_t &epoch) const;
};

}  // namespace yappl
