#include "network/wifi_manager.h"

#include <WiFi.h>

#include "app/config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define YAPPL_WIFI_SSID ""
#define YAPPL_WIFI_PASSWORD ""
#endif

namespace yappl {

bool WifiManager::begin() {
  if (!AppConfig::enableWifi) {
    Serial.println(F("Wi-Fi disabled in AppConfig"));
    return false;
  }

  ssid_ = YAPPL_WIFI_SSID;
  configured_ = ssid_.length() > 0 && String(YAPPL_WIFI_PASSWORD).length() > 0;
  if (!configured_) {
    Serial.println(F("Wi-Fi not configured. Copy include/secrets.example.h to include/secrets.h."));
    return false;
  }

  Serial.printf("Connecting Wi-Fi: %s\n", ssid_.c_str());

  // Station mode means Yappl connects to your router. It is not creating its
  // own hotspot yet; captive portal setup can come later.
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(YAPPL_WIFI_SSID, YAPPL_WIFI_PASSWORD);

  const uint32_t startedAtMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAtMs < AppConfig::wifiConnectTimeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (!isConnected()) {
    Serial.println(F("Wi-Fi connection failed"));
    return false;
  }

  Serial.printf("Wi-Fi connected. IP: %s\n", ipAddress().c_str());
  return true;
}

bool WifiManager::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

String WifiManager::ipAddress() const {
  if (!isConnected()) {
    return "";
  }
  return WiFi.localIP().toString();
}

}  // namespace yappl
