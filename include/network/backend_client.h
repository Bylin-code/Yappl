#pragma once

#include <Arduino.h>
#include <HTTPClient.h>

namespace yappl {

// Backend status payload parsed from /device/status. This is intentionally tiny
// while the backend is still proving basic connectivity.
struct BackendStatus {
  bool requestOk = false;
  bool hasYappedToday = false;
};

// Small HTTP client for Yappl's backend API. It owns URL construction, auth
// headers, timeouts, and response-code logging so app code can stay focused on
// product behavior.
class BackendClient {
 public:
  bool begin();
  bool isConfigured() const { return configured_; }

  bool ping(bool wifiConnected, bool timeSynced, const char *modeName);
  bool sendYapCompleted(uint64_t completedAtEpoch);
  BackendStatus fetchStatus();

 private:
  String urlFor(const char *path) const;
  void addAuthHeader(HTTPClient &http) const;
  bool responseHasTrue(const String &body, const char *fieldName) const;

  bool configured_ = false;
  String baseUrl_;
  String deviceId_;
  String deviceSecret_;
};

}  // namespace yappl
