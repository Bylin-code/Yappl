#pragma once

#include <Arduino.h>

namespace yappl {

// Tiny persistent store for journal history metadata. It uses ESP32 NVS flash,
// so values survive reset and power loss without needing an SD card.
class YapHistoryStore {
 public:
  bool begin();

  bool hasLastYap() const { return hasLastYap_; }
  uint64_t lastYapEpoch() const { return lastYapEpoch_; }

  bool saveLastYapEpoch(uint64_t epoch);
  bool clearLastYap();
  bool resetForNewFirmware(const char *firmwareBuildId);

 private:
  bool started_ = false;
  bool hasLastYap_ = false;
  uint64_t lastYapEpoch_ = 0;
};

}  // namespace yappl
