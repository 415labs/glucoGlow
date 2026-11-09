#pragma once
// Runtime status shared between main loop and the web app (read-only for the web app).
#include <stdint.h>
#include "glucose.h"

#ifndef FW_VERSION
#define FW_VERSION "0.3.0"
#endif

// Name of the on-demand setup WiFi network (password is generated per device and shown on the display)
#define AP_SSID "GlucoGlow-setup"

struct DeviceStatus {
  bool provisioning = false; // setup WiFi is on because there is no working WiFi connection
  bool timeValid = false;    // NTP synced
  bool night = false;
  State state = State::Unknown;
  bool haveValue = false;
  int mgdl = 0;
  int trendDir = 0;
  bool trendKnown = false;
  int ageMin = 0;
  bool ageKnown = false;
  bool pollEver = false;
  bool lastOk = false;
  char lastErr[80] = {0};
  uint32_t lastPollMs = 0;   // millis() of last poll attempt
  uint32_t failCount = 0;
};
