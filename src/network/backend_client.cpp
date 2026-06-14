#include "network/backend_client.h"

#include <HTTPClient.h>

#include "app/config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef YAPPL_BACKEND_BASE_URL
#define YAPPL_BACKEND_BASE_URL ""
#endif

#ifndef YAPPL_DEVICE_ID
#define YAPPL_DEVICE_ID "yappl_dev_001"
#endif

#ifndef YAPPL_DEVICE_SECRET
#define YAPPL_DEVICE_SECRET "local_dev_secret"
#endif

namespace yappl {
namespace {

bool stringLooksConfigured(const char *value) {
  return value != nullptr && value[0] != '\0';
}

}  // namespace

bool BackendClient::begin() {
  if (!AppConfig::enableBackend) {
    Serial.println(F("Backend disabled in AppConfig"));
    return false;
  }

  configured_ = stringLooksConfigured(YAPPL_BACKEND_BASE_URL) &&
                stringLooksConfigured(YAPPL_DEVICE_ID) &&
                stringLooksConfigured(YAPPL_DEVICE_SECRET);
  if (!configured_) {
    Serial.println(F("Backend not configured. Set YAPPL_BACKEND_BASE_URL in include/secrets.h."));
    return false;
  }

  baseUrl_ = YAPPL_BACKEND_BASE_URL;
  deviceId_ = YAPPL_DEVICE_ID;
  deviceSecret_ = YAPPL_DEVICE_SECRET;

  // Avoid accidental double slashes when paths are appended.
  while (baseUrl_.endsWith("/")) {
    baseUrl_.remove(baseUrl_.length() - 1);
  }

  Serial.printf("Backend configured: %s\n", baseUrl_.c_str());
  return true;
}

bool BackendClient::ping(bool wifiConnected, bool timeSynced, const char *modeName) {
  if (!configured_) {
    return false;
  }

  HTTPClient http;
  http.setTimeout(AppConfig::backendHttpTimeoutMs);
  if (!http.begin(urlFor("/device/ping"))) {
    Serial.println(F("Backend ping failed: bad URL"));
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  addAuthHeader(http);

  String body = "{";
  body += "\"device_id\":\"" + deviceId_ + "\",";
  body += "\"wifi_connected\":" + String(wifiConnected ? "true" : "false") + ",";
  body += "\"time_synced\":" + String(timeSynced ? "true" : "false") + ",";
  body += "\"mode\":\"" + String(modeName != nullptr ? modeName : "unknown") + "\"";
  body += "}";

  const int code = http.POST(body);
  http.end();

  const bool ok = code >= 200 && code < 300;
  Serial.printf("Backend ping %s: HTTP %d\n", ok ? "OK" : "failed", code);
  return ok;
}

bool BackendClient::sendYapCompleted(uint64_t completedAtEpoch) {
  if (!configured_) {
    return false;
  }

  HTTPClient http;
  http.setTimeout(AppConfig::backendHttpTimeoutMs);
  if (!http.begin(urlFor("/device/yap-completed"))) {
    Serial.println(F("Backend yap-completed failed: bad URL"));
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  addAuthHeader(http);

  String body = "{";
  body += "\"device_id\":\"" + deviceId_ + "\",";
  body += "\"completed_at_epoch\":" + String(static_cast<unsigned long long>(completedAtEpoch));
  body += "}";

  const int code = http.POST(body);
  http.end();

  const bool ok = code >= 200 && code < 300;
  Serial.printf("Backend yap-completed %s: HTTP %d\n", ok ? "OK" : "failed", code);
  return ok;
}

BackendStatus BackendClient::fetchStatus() {
  BackendStatus status;
  if (!configured_) {
    return status;
  }

  HTTPClient http;
  http.setTimeout(AppConfig::backendHttpTimeoutMs);
  const String path = "/device/status?device_id=" + deviceId_;
  if (!http.begin(urlFor(path.c_str()))) {
    Serial.println(F("Backend status failed: bad URL"));
    return status;
  }

  addAuthHeader(http);

  const int code = http.GET();
  const String body = http.getString();
  http.end();

  status.requestOk = code >= 200 && code < 300;
  status.hasYappedToday = status.requestOk && responseHasTrue(body, "has_yapped_today");
  Serial.printf("Backend status %s: HTTP %d yapped_today=%s\n",
                status.requestOk ? "OK" : "failed",
                code,
                status.hasYappedToday ? "true" : "false");
  return status;
}

String BackendClient::urlFor(const char *path) const {
  return baseUrl_ + path;
}

void BackendClient::addAuthHeader(HTTPClient &http) const {
  http.addHeader("Authorization", "Bearer " + deviceSecret_);
}

bool BackendClient::responseHasTrue(const String &body, const char *fieldName) const {
  const String pattern = "\"" + String(fieldName) + "\":true";
  String compact = body;
  compact.replace(" ", "");
  compact.replace("\n", "");
  compact.replace("\r", "");
  compact.replace("\t", "");
  return compact.indexOf(pattern) >= 0;
}

}  // namespace yappl

