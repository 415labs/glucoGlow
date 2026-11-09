#pragma once
// Typed settings struct + defaults + validation. Pure C++ (no Arduino deps) so it can be unit tested.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum AlertMode : uint8_t {
  ALERT_STEADY = 0,
  ALERT_BLINK_SLOW = 1,
  ALERT_BLINK_FAST = 2,
  ALERT_PULSE = 3,
  ALERT_TRIPLE = 4  // 3 quick blinks, then a pause (port of glucoLed2 "issue" pattern)
};

struct Settings {
  // Connection
  char wifiSsid[33];
  char wifiPass[65];
  char lluEmail[65];
  char lluPass[65];
  char lluRegion[8];     // "" = auto (login redirect), or e.g. "us", "eu"
  char lluVersion[12];   // app version header sent to LibreLinkUp; raise it if login says "outdated"
  bool tlsInsecure;      // skip certificate validation (fallback only)
  uint16_t pollSec;      // 30..900, default 60

  // Password of the on-demand setup WiFi. Generated on first boot, shown on the display.
  char apPass[33];

  // Units and ranges (always stored in mg/dL)
  //   below criticalLow ........ Critical low  (red)
  //   criticalLow..normalLow-1 . Caution       (orange)
  //   normalLow..normalHigh .... Normal        (green)
  //   normalHigh+1..criticalHigh Caution       (orange)
  //   above criticalHigh ....... Critical high (red)
  uint8_t units;         // 0 = mg/dL, 1 = mmol/L
  uint16_t criticalLow;  // default 60
  uint16_t normalLow;    // default 90
  uint16_t normalHigh;   // default 150
  uint16_t criticalHigh; // default 200
  uint16_t staleMin;     // minutes before data counts as stale, default 15

  // Alert patterns per state
  uint8_t criticalLowMode, criticalHighMode, cautionMode, staleMode;

  // Brightness schedule. The display is never blanked: the dimmest level is still visible.
  uint16_t dayStartMin;      // minutes after midnight, default 07:00
  uint16_t nightStartMin;    // default 22:00
  uint8_t dayContrast;       // OLED 1..255
  uint8_t nightContrast;     // OLED 1..255
  char tz[48];               // POSIX TZ string

  // Display options
  bool showArrow, showAge, pixelShift;
  bool showChart;            // 12 h chart: web app + a chart page on the display
  uint16_t rotation;         // screen rotation in degrees clockwise: 0, 90, 180, 270

  // Device
  char hostname[24];
  char adminPass[33];        // empty = no auth on the web app when reached over your WiFi
};

inline void copyStr(char* dst, size_t n, const char* src) {
  if (!n) return;
  strncpy(dst, src ? src : "", n - 1);
  dst[n - 1] = 0;
}

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline void settingsDefaults(Settings& s) {
  memset(&s, 0, sizeof(s));
  copyStr(s.lluVersion, sizeof s.lluVersion, "4.16.0");
  s.tlsInsecure = false;
  s.pollSec = 60;
  s.units = 0;
  s.criticalLow = 60;
  s.normalLow = 90;
  s.normalHigh = 150;
  s.criticalHigh = 200;
  s.staleMin = 15;
  s.criticalLowMode = ALERT_BLINK_FAST;
  s.criticalHighMode = ALERT_BLINK_SLOW;
  s.cautionMode = ALERT_PULSE;
  s.staleMode = ALERT_TRIPLE;
  s.dayStartMin = 7 * 60;
  s.nightStartMin = 22 * 60;
  s.dayContrast = 160;
  s.nightContrast = 4;
  copyStr(s.tz, sizeof s.tz, "PST8PDT,M3.2.0,M11.1.0");
  s.showArrow = s.showAge = s.pixelShift = s.showChart = true;
  copyStr(s.hostname, sizeof s.hostname, "glucoglow");
}

// Force every field into a safe range. Always call after loading or applying user input.
inline void settingsClamp(Settings& s) {
  s.wifiSsid[sizeof s.wifiSsid - 1] = 0;
  s.wifiPass[sizeof s.wifiPass - 1] = 0;
  s.lluEmail[sizeof s.lluEmail - 1] = 0;
  s.lluPass[sizeof s.lluPass - 1] = 0;
  s.lluRegion[sizeof s.lluRegion - 1] = 0;
  s.tz[sizeof s.tz - 1] = 0;
  s.adminPass[sizeof s.adminPass - 1] = 0;
  s.apPass[sizeof s.apPass - 1] = 0;
  if (strlen(s.apPass) < 8) s.apPass[0] = 0;  // WPA2 needs 8+ chars; empty = generate a new one
  if (!s.tz[0]) copyStr(s.tz, sizeof s.tz, "PST8PDT,M3.2.0,M11.1.0");
  s.lluVersion[sizeof s.lluVersion - 1] = 0;
  if (!s.lluVersion[0]) copyStr(s.lluVersion, sizeof s.lluVersion, "4.16.0");

  s.pollSec = (uint16_t)clampi(s.pollSec, 30, 900);
  s.units = s.units ? 1 : 0;

  // Ranges must stay ordered: criticalLow < normalLow <= normalHigh < criticalHigh
  s.criticalLow = (uint16_t)clampi(s.criticalLow, 30, 100);
  s.normalLow = (uint16_t)clampi(s.normalLow, s.criticalLow + 1, 200);
  s.normalHigh = (uint16_t)clampi(s.normalHigh, s.normalLow, 350);
  s.criticalHigh = (uint16_t)clampi(s.criticalHigh, s.normalHigh + 1, 450);
  s.staleMin = (uint16_t)clampi(s.staleMin, 5, 120);

  if (s.criticalLowMode > ALERT_TRIPLE) s.criticalLowMode = ALERT_BLINK_FAST;
  if (s.criticalHighMode > ALERT_TRIPLE) s.criticalHighMode = ALERT_BLINK_SLOW;
  if (s.cautionMode > ALERT_TRIPLE) s.cautionMode = ALERT_PULSE;
  if (s.staleMode > ALERT_TRIPLE) s.staleMode = ALERT_TRIPLE;

  s.dayStartMin %= 1440;
  s.nightStartMin %= 1440;
  if (s.dayContrast < 1) s.dayContrast = 1;      // never fully dark: always visible
  if (s.nightContrast < 1) s.nightContrast = 1;
  if (s.rotation != 90 && s.rotation != 180 && s.rotation != 270) s.rotation = 0;

  // hostname: lowercase letters, digits, '-' only
  char clean[sizeof s.hostname];
  size_t j = 0;
  for (size_t i = 0; s.hostname[i] && j < sizeof clean - 1; i++) {
    char c = s.hostname[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') clean[j++] = c;
  }
  clean[j] = 0;
  copyStr(s.hostname, sizeof s.hostname, j ? clean : "glucoglow");
}
