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
