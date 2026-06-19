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

#if defined(YAPPL_BACKEND_BASE_URLS)
const char *const kBackendBaseUrls[] = YAPPL_BACKEND_BASE_URLS;
#else
const char *const kBackendBaseUrls[] = {
    YAPPL_BACKEND_BASE_URL,
};
#endif

constexpr size_t kBackendBaseUrlCount = sizeof(kBackendBaseUrls) / sizeof(kBackendBaseUrls[0]);

bool stringLooksConfigured(const char *value) {
  return value != nullptr && value[0] != '\0';
}

String normalizedBaseUrl(const char *value) {
  String url = value;
  while (url.endsWith("/")) {
    url.remove(url.length() - 1);
  }
  return url;
}

}  // namespace

bool BackendClient::begin() {
  if (!AppConfig::enableBackend) {
    Serial.println(F("Backend disabled in AppConfig"));
    return false;
  }

  configured_ = stringLooksConfigured(YAPPL_DEVICE_ID) &&
                stringLooksConfigured(YAPPL_DEVICE_SECRET);
  bool hasBackendUrl = false;
  for (const char *url : kBackendBaseUrls) {
    if (stringLooksConfigured(url)) {
      hasBackendUrl = true;
      break;
    }
  }
  configured_ = configured_ && hasBackendUrl;
  if (!configured_) {
    Serial.println(F("Backend not configured. Set YAPPL_BACKEND_BASE_URLS or YAPPL_BACKEND_BASE_URL in include/secrets.h."));
    return false;
  }

  deviceId_ = YAPPL_DEVICE_ID;
  deviceSecret_ = YAPPL_DEVICE_SECRET;

  if (!selectInitialBackend()) {
    Serial.println(F("No backend responded during startup; using first configured backend."));
    for (size_t i = 0; i < kBackendBaseUrlCount; ++i) {
      if (stringLooksConfigured(kBackendBaseUrls[i])) {
        backendIndex_ = i;
        baseUrl_ = normalizedBaseUrl(kBackendBaseUrls[i]);
        break;
      }
    }
  }

  Serial.printf("Backend configured: %s\n", baseUrl_.c_str());
  return true;
}

BackendStatus BackendClient::ping(bool wifiConnected, bool timeSynced, const char *modeName) {
  BackendStatus status;
  if (!configured_) {
    return status;
  }

  HTTPClient http;
  http.setTimeout(AppConfig::backendHttpTimeoutMs);
  if (!http.begin(urlFor("/device/ping"))) {
    Serial.println(F("Backend ping failed: bad URL"));
    return status;
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
  const String response = http.getString();
  http.end();

  status.requestOk = code >= 200 && code < 300;
  if (status.requestOk) {
    status.lastYapCompletedAtEpoch = jsonUnsignedValue(response, "last_yap_completed_at_epoch");
    status.mode = jsonStringValue(response, "mode");
  }
  Serial.printf("Backend ping %s: HTTP %d url=%s mode=%s last_yap_epoch=%llu\n",
                status.requestOk ? "OK" : "failed",
                code,
                baseUrl_.c_str(),
                status.mode.c_str(),
                static_cast<unsigned long long>(status.lastYapCompletedAtEpoch));
  markRequestResult(status.requestOk);
  return status;
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
  return markRequestResult(ok);
}

String BackendClient::startAudioSession(uint64_t startedAtEpoch, uint32_t sampleRateHz) {
  if (!configured_) {
    return "";
  }

  HTTPClient http;
  http.setTimeout(AppConfig::backendHttpTimeoutMs);
  if (!http.begin(urlFor("/device/session/start"))) {
    Serial.println(F("Backend session start failed: bad URL"));
    return "";
  }

  http.addHeader("Content-Type", "application/json");
  addAuthHeader(http);

  String body = "{";
  body += "\"device_id\":\"" + deviceId_ + "\",";
  body += "\"sample_rate_hz\":" + String(sampleRateHz) + ",";
  body += "\"sample_format\":\"pcm_s16le\",";
  body += "\"started_at_epoch\":" + String(static_cast<unsigned long long>(startedAtEpoch));
  body += "}";

  const int code = http.POST(body);
  const String response = http.getString();
  http.end();

  const bool ok = code >= 200 && code < 300;
  const String sessionId = ok ? jsonStringValue(response, "session_id") : "";
  Serial.printf("Backend session start %s: HTTP %d session=%s\n",
                ok && sessionId.length() > 0 ? "OK" : "failed",
                code,
                sessionId.c_str());
  markRequestResult(ok && sessionId.length() > 0);
  return sessionId;
}

bool BackendClient::uploadAudioChunk(const String &sessionId, const uint8_t *data, size_t byteCount) {
  if (!configured_ || sessionId.length() == 0 || data == nullptr || byteCount == 0) {
    return false;
  }

  HTTPClient http;
  http.setTimeout(AppConfig::backendHttpTimeoutMs);
  const String path = "/device/session/audio?session_id=" + sessionId;
  if (!http.begin(urlFor(path.c_str()))) {
    Serial.println(F("Backend audio upload failed: bad URL"));
    return false;
  }

  http.addHeader("Content-Type", "application/octet-stream");
  addAuthHeader(http);

  const int code = http.POST(const_cast<uint8_t *>(data), byteCount);
  http.end();

  const bool ok = code >= 200 && code < 300;
  if (!ok) {
    Serial.printf("Backend audio upload failed: HTTP %d bytes=%u\n", code, static_cast<unsigned>(byteCount));
  }
  return markRequestResult(ok);
}

bool BackendClient::finishAudioSession(const String &sessionId, uint64_t completedAtEpoch) {
  if (!configured_ || sessionId.length() == 0) {
    return false;
  }

  HTTPClient http;
  http.setTimeout(AppConfig::backendHttpTimeoutMs);
  if (!http.begin(urlFor("/device/session/finish"))) {
    Serial.println(F("Backend session finish failed: bad URL"));
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  addAuthHeader(http);

  String body = "{";
  body += "\"device_id\":\"" + deviceId_ + "\",";
  body += "\"session_id\":\"" + sessionId + "\",";
  body += "\"completed_at_epoch\":" + String(static_cast<unsigned long long>(completedAtEpoch));
  body += "}";

  const int code = http.POST(body);
  http.end();

  const bool ok = code >= 200 && code < 300;
  Serial.printf("Backend session finish %s: HTTP %d session=%s\n", ok ? "OK" : "failed", code, sessionId.c_str());
  return markRequestResult(ok);
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
  if (status.requestOk) {
    status.lastYapCompletedAtEpoch = jsonUnsignedValue(body, "last_yap_completed_at_epoch");
    status.mode = jsonStringValue(body, "mode");
  }
  Serial.printf("Backend status %s: HTTP %d mode=%s last_yap_epoch=%llu\n",
                status.requestOk ? "OK" : "failed",
                code,
                status.mode.c_str(),
                static_cast<unsigned long long>(status.lastYapCompletedAtEpoch));
  markRequestResult(status.requestOk);
  return status;
}

bool BackendClient::selectInitialBackend() {
  for (size_t i = 0; i < kBackendBaseUrlCount; ++i) {
    if (!stringLooksConfigured(kBackendBaseUrls[i])) {
      continue;
    }

    const String candidate = normalizedBaseUrl(kBackendBaseUrls[i]);
    Serial.printf("Checking backend: %s\n", candidate.c_str());
    if (tryHealthCheck(candidate)) {
      backendIndex_ = i;
      baseUrl_ = candidate;
      return true;
    }
  }
  return false;
}

bool BackendClient::tryHealthCheck(const String &baseUrl) const {
  HTTPClient http;
  http.setTimeout(AppConfig::backendHttpTimeoutMs);
  if (!http.begin(baseUrl + "/health")) {
    return false;
  }

  const int code = http.GET();
  http.end();
  return code >= 200 && code < 300;
}

bool BackendClient::markRequestResult(bool ok) {
  if (!ok) {
    moveToNextBackend();
  }
  return ok;
}

void BackendClient::moveToNextBackend() {
  if (kBackendBaseUrlCount <= 1) {
    return;
  }

  for (size_t offset = 1; offset <= kBackendBaseUrlCount; ++offset) {
    const size_t nextIndex = (backendIndex_ + offset) % kBackendBaseUrlCount;
    if (!stringLooksConfigured(kBackendBaseUrls[nextIndex])) {
      continue;
    }

    backendIndex_ = nextIndex;
    baseUrl_ = normalizedBaseUrl(kBackendBaseUrls[nextIndex]);
    Serial.printf("Backend failover selected: %s\n", baseUrl_.c_str());
    return;
  }
}

String BackendClient::urlFor(const char *path) const {
  return baseUrl_ + path;
}

void BackendClient::addAuthHeader(HTTPClient &http) const {
  http.addHeader("Authorization", "Bearer " + deviceSecret_);
}

String BackendClient::jsonStringValue(const String &body, const char *fieldName) const {
  // Tiny parser for simple backend responses. We can replace this with a JSON
  // library if responses become nested or complex.
  const String key = "\"" + String(fieldName) + "\"";
  int index = body.indexOf(key);
  if (index < 0) {
    return "";
  }

  index = body.indexOf(':', index);
  if (index < 0) {
    return "";
  }

  index = body.indexOf('"', index);
  if (index < 0) {
    return "";
  }
  const int start = index + 1;
  const int end = body.indexOf('"', start);
  if (end < 0) {
    return "";
  }
  return body.substring(start, end);
}

uint64_t BackendClient::jsonUnsignedValue(const String &body, const char *fieldName) const {
  const String key = "\"" + String(fieldName) + "\"";
  int index = body.indexOf(key);
  if (index < 0) {
    return 0;
  }

  index = body.indexOf(':', index);
  if (index < 0) {
    return 0;
  }

  String compact = body;
  compact.replace(" ", "");
  compact.replace("\n", "");
  compact.replace("\r", "");
  compact.replace("\t", "");

  index = compact.indexOf(key);
  if (index < 0) {
    return 0;
  }
  index = compact.indexOf(':', index);
  if (index < 0 || index + 1 >= compact.length()) {
    return 0;
  }

  const int start = index + 1;
  int end = start;
  while (end < compact.length() && isDigit(compact[end])) {
    ++end;
  }
  if (end == start) {
    return 0;
  }

  return strtoull(compact.substring(start, end).c_str(), nullptr, 10);
}

}  // namespace yappl
