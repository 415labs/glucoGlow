#pragma once
// 72x40 OLED rendering, day/night brightness, alert animations, text screens.
// The display is never blanked: the night level is dim but always visible.
#include <Arduino.h>
#include "glucose.h"
#include "history.h"
#include "settings.h"

struct View {
  State state = State::Unknown;
  bool haveValue = false;
  int mgdl = 0;
  bool trendKnown = false;
  int trendDir = 0;      // -2..+2
  bool ageKnown = false;
  int ageMin = 0;
};

class Display {
 public:
  void begin();
  void setHistory(const History* h) { hist_ = h; }
  void applySettings(const Settings& s);  // (re)configures the screen rotation
  // Call often (every ~50 ms). `timeValid`/`minuteOfDay` drive the day/night schedule.
  void update(const View& v, const Settings& s, bool timeValid, int minuteOfDay);

  // Web-app preview: show a state (or keep the real one) at a given brightness for N seconds.
  void preview(State st, bool keepState, uint8_t contrast, uint16_t seconds);

  // Immediate text screen (boot, blocking operations).
  void showNow(const char* l1, const char* l2 = "", const char* l3 = "", const char* l4 = "");
  // Text screen shown by update() instead of the glucose view until it expires.
  void overlay(const char* l1, const char* l2 = "", const char* l3 = "", const char* l4 = "",
               uint32_t ms = 4000);
  void clearOverlay();
  bool overlayActive() const;
  bool isNight() const { return night_; }

 private:
  void drawView(const View& v, const Settings& s, int sx, int sy);
  void drawArrow(int cx, int cy, int dir);
  void setContrastIfChanged(uint8_t c);
  void drawChart(const Settings& s, int32_t nowS);

  const History* hist_ = nullptr;
  char ov_[4][36] = {{0}, {0}, {0}, {0}};
  uint32_t ovUntil_ = 0;
  bool ovActive_ = false;

  uint32_t previewUntil_ = 0;
  State previewState_ = State::Normal;
  bool previewKeep_ = true;
  uint8_t previewContrast_ = 30;

  uint8_t lastContrast_ = 255;
  bool night_ = false;

  uint16_t rotation_ = 0;
  bool rotationInit_ = false;
  bool portrait_ = false;  // 90/270 degrees: the 72x40 panel becomes a 40x72 portrait screen
};
