#pragma once
// On-device web app (HTTP, LAN / setup WiFi only): settings UI + JSON API.
#include <Arduino.h>
#include "glucose.h"
#include "history.h"
#include "settings.h"
#include "status.h"

struct WebUiHooks {
  void (*onSettingsSaved)() = nullptr;  // apply settings live (tz, poll, rotation, ...)
  // Show a state (or keep the real one when keepState) at a brightness for N seconds.
  void (*onPreview)(State state, bool keepState, uint8_t contrast, uint16_t secs) = nullptr;
};

class WebUi {
 public:
  // `settings` and `status` must outlive the WebUi.
  void begin(Settings* settings, const DeviceStatus* status, const WebUiHooks& hooks);
  // Tell the web server whether the setup WiFi (access point) is on: enables the captive-portal DNS
  // and lets clients of the setup WiFi in without the admin password (the WiFi password protects it).
  void setSetupMode(bool on);
  void setHistory(const History* h);  // data for GET /api/history
  void handle();  // call every loop()
};
