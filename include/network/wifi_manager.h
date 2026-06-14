#pragma once

#include <Arduino.h>

namespace yappl {

struct WifiCredential {
  const char *ssid;
  const char *password;
};

// Small wrapper around Arduino WiFi. The rest of the app should ask this class
// for connection status instead of calling WiFi directly everywhere.
class WifiManager {
 public:
  bool begin();
  bool isConfigured() const { return configured_; }
  bool isConnected() const;
  String ipAddress() const;
  String ssid() const { return ssid_; }

 private:
  bool configured_ = false;
  String ssid_;
};

}  // namespace yappl
