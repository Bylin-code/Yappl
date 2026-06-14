#include "network/wifi_manager.h"

#include <WiFi.h>

#include "app/config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define YAPPL_WIFI_SSID ""
#define YAPPL_WIFI_PASSWORD ""
#endif

#ifndef YAPPL_WIFI_SSID
#define YAPPL_WIFI_SSID ""
#endif

#ifndef YAPPL_WIFI_PASSWORD
#define YAPPL_WIFI_PASSWORD ""
#endif

namespace yappl {
namespace {

#if defined(YAPPL_WIFI_NETWORKS)
const WifiCredential kWifiCredentials[] = YAPPL_WIFI_NETWORKS;
#else
const WifiCredential kWifiCredentials[] = {
    {YAPPL_WIFI_SSID, YAPPL_WIFI_PASSWORD},
};
#endif

constexpr size_t kWifiCredentialCount = sizeof(kWifiCredentials) / sizeof(kWifiCredentials[0]);

bool credentialLooksValid(const WifiCredential &credential) {
  return credential.ssid != nullptr &&
         credential.password != nullptr &&
         credential.ssid[0] != '\0' &&
         credential.password[0] != '\0';
}

}  // namespace

bool WifiManager::begin() {
  if (!AppConfig::enableWifi) {
    Serial.println(F("Wi-Fi disabled in AppConfig"));
    return false;
  }

  configured_ = false;
  for (const WifiCredential &credential : kWifiCredentials) {
    if (credentialLooksValid(credential)) {
      configured_ = true;
      break;
    }
  }

  if (!configured_) {
    Serial.println(F("Wi-Fi not configured. Copy include/secrets.example.h to include/secrets.h."));
    return false;
  }

  // Station mode means Yappl connects to your router. It is not creating its
  // own hotspot yet; captive portal setup can come later.
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  for (const WifiCredential &credential : kWifiCredentials) {
    if (!credentialLooksValid(credential)) {
      continue;
    }

    ssid_ = credential.ssid;
    Serial.printf("Connecting Wi-Fi: %s\n", ssid_.c_str());

    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(credential.ssid, credential.password);

    const uint32_t startedAtMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startedAtMs < AppConfig::wifiConnectTimeoutMs) {
      delay(250);
      Serial.print('.');
    }
    Serial.println();

    if (isConnected()) {
      Serial.printf("Wi-Fi connected. IP: %s\n", ipAddress().c_str());
      return true;
    }

    Serial.printf("Wi-Fi connection failed: %s\n", ssid_.c_str());
  }

  ssid_ = "";
  Serial.println(F("All configured Wi-Fi networks failed"));
  return false;
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
