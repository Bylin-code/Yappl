#pragma once

// Copy this file to include/secrets.h, then fill in your real Wi-Fi networks.
// include/secrets.h is ignored by git so your passwords do not get committed.
//
// Yappl tries these in order and connects to the first one that works.
#define YAPPL_WIFI_NETWORKS \
  {                         \
    {"Home WiFi", "Home WiFi Password"},       \
    {"Phone Hotspot", "Hotspot Password"},     \
    {"Other Network", "Other Password"},       \
  }

// Local/backend settings. Use your computer or Mac mini LAN IP here, not
// localhost. The ESP32's localhost is the ESP32 itself. Yappl tries these in
// order at boot and switches to the next configured URL when a request fails.
#define YAPPL_BACKEND_BASE_URLS \
  {                             \
    "http://192.168.1.25:8000", \
    "http://192.168.1.50:8000", \
    "https://api.yappl.example.com", \
  }
#define YAPPL_DEVICE_ID "yappl_dev_001"
#define YAPPL_DEVICE_SECRET "local_dev_secret"
