#include "storage/yap_history_store.h"

#include <Preferences.h>

namespace yappl {
namespace {

constexpr const char *kNamespace = "yappl";
constexpr const char *kLastYapEpochKey = "last_yap";
constexpr const char *kLastYapLowKey = "last_lo";
constexpr const char *kLastYapHighKey = "last_hi";
constexpr const char *kLastYapValidKey = "last_ok";
constexpr const char *kFirmwareBuildKey = "fw_build";

}  // namespace

bool YapHistoryStore::begin() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    Serial.println(F("Failed to open yap history store"));
    return false;
  }

  const bool hasSplitValue = prefs.getBool(kLastYapValidKey, false);
  if (hasSplitValue) {
    const uint64_t low = prefs.getUInt(kLastYapLowKey, 0);
    const uint64_t high = prefs.getUInt(kLastYapHighKey, 0);
    lastYapEpoch_ = (high << 32) | low;
  } else {
    // Backward compatibility with the older single 64-bit key.
    lastYapEpoch_ = prefs.getULong64(kLastYapEpochKey, 0);
  }
  hasLastYap_ = lastYapEpoch_ > 0;
  prefs.end();

  started_ = true;
  if (hasLastYap_) {
    Serial.printf("Last yap epoch: %llu\n", static_cast<unsigned long long>(lastYapEpoch_));
  } else {
    Serial.println(F("No stored yap history yet"));
  }
  return true;
}

bool YapHistoryStore::saveLastYapEpoch(uint64_t epoch) {
  if (!started_ || epoch == 0) {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    Serial.println(F("Failed to open yap history store for write"));
    return false;
  }

  const uint32_t low = static_cast<uint32_t>(epoch & 0xffffffffULL);
  const uint32_t high = static_cast<uint32_t>((epoch >> 32) & 0xffffffffULL);

  const size_t wroteLow = prefs.putUInt(kLastYapLowKey, low);
  const size_t wroteHigh = prefs.putUInt(kLastYapHighKey, high);
  const size_t wroteValid = prefs.putBool(kLastYapValidKey, true);

  const uint64_t verifiedLow = prefs.getUInt(kLastYapLowKey, 0);
  const uint64_t verifiedHigh = prefs.getUInt(kLastYapHighKey, 0);
  const uint64_t verified = (verifiedHigh << 32) | verifiedLow;
  prefs.end();

  if (wroteLow == 0 || wroteHigh == 0 || wroteValid == 0 || verified != epoch) {
    Serial.println(F("Failed to save last yap epoch"));
    return false;
  }

  lastYapEpoch_ = epoch;
  hasLastYap_ = true;
  Serial.printf("Saved last yap epoch: %llu\n", static_cast<unsigned long long>(lastYapEpoch_));
  return true;
}

bool YapHistoryStore::clearLastYap() {
  if (!started_) {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    Serial.println(F("Failed to open yap history store for clear"));
    return false;
  }

  const bool removed = prefs.remove(kLastYapEpochKey);
  prefs.remove(kLastYapLowKey);
  prefs.remove(kLastYapHighKey);
  prefs.remove(kLastYapValidKey);
  prefs.end();

  lastYapEpoch_ = 0;
  hasLastYap_ = false;
  Serial.println(removed ? F("Cleared last yap epoch") : F("Last yap epoch was already empty"));
  return true;
}

bool YapHistoryStore::resetForNewFirmware(const char *firmwareBuildId) {
  if (!started_ || firmwareBuildId == nullptr || firmwareBuildId[0] == '\0') {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    Serial.println(F("Failed to open yap history store for firmware check"));
    return false;
  }

  const String previousBuild = prefs.getString(kFirmwareBuildKey, "");
  const String currentBuild = firmwareBuildId;
  if (previousBuild == currentBuild) {
    prefs.end();
    return false;
  }

  prefs.remove(kLastYapEpochKey);
  prefs.remove(kLastYapLowKey);
  prefs.remove(kLastYapHighKey);
  prefs.remove(kLastYapValidKey);
  prefs.putString(kFirmwareBuildKey, currentBuild);
  prefs.end();

  lastYapEpoch_ = 0;
  hasLastYap_ = false;
  Serial.println(F("New firmware build detected; cleared last yap epoch"));
  return true;
}

}  // namespace yappl
