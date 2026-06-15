#pragma once

#include <Arduino.h>
#include <HTTPClient.h>

namespace yappl {

// Backend status payload parsed from /device/status. This is intentionally tiny
// while the backend is still proving basic connectivity.
struct BackendStatus {
  bool requestOk = false;
  uint64_t lastYapCompletedAtEpoch = 0;
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
  String startAudioSession(uint64_t startedAtEpoch, uint32_t sampleRateHz);
  bool uploadAudioChunk(const String &sessionId, const uint8_t *data, size_t byteCount);
  bool finishAudioSession(const String &sessionId, uint64_t completedAtEpoch);
  BackendStatus fetchStatus();

 private:
  bool selectInitialBackend();
  bool tryHealthCheck(const String &baseUrl) const;
  bool markRequestResult(bool ok);
  void moveToNextBackend();
  String urlFor(const char *path) const;
  void addAuthHeader(HTTPClient &http) const;
  String jsonStringValue(const String &body, const char *fieldName) const;
  uint64_t jsonUnsignedValue(const String &body, const char *fieldName) const;

  bool configured_ = false;
  size_t backendIndex_ = 0;
  String baseUrl_;
  String deviceId_;
  String deviceSecret_;
};

}  // namespace yappl
