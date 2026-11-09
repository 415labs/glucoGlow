#include "display.h"

#include <U8g2lib.h>
#include <math.h>
#include <string.h>

// Board: SSD1306 72x40 window, SCL=GPIO6, SDA=GPIO5.
// Rotation is applied with setDisplayRotation(); at 90/270 degrees the logical screen is 40x72.
static U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 6, 5);

static AlertMode modeFor(State st, const Settings& s) {
  switch (st) {
    case State::CriticalLow: return (AlertMode)s.criticalLowMode;
    case State::CriticalHigh: return (AlertMode)s.criticalHighMode;
    case State::CautionLow:
    case State::CautionHigh: return (AlertMode)s.cautionMode;
    case State::Stale: return (AlertMode)s.staleMode;
    default: return ALERT_STEADY;
  }
}

static const char* labelFor(State st) {
  switch (st) {
    case State::CriticalLow: return "CRIT LOW";
    case State::CautionLow: return "LOW";
    case State::CautionHigh: return "HIGH";
    case State::CriticalHigh: return "CRIT HIGH";
    case State::Stale: return "STALE";
    default: return "";
  }
}

// ---------------------------------------------------------------------------------------------
// Text screens: instructions are drawn as large as they fit (about twice the old size)
// ---------------------------------------------------------------------------------------------

// Splits `t` into rows of at most maxCh characters, preferring to break at a space (dropped) or
// right after '-' / '.'. Appends to rows[], returns the new row count (stops at maxRows).
static const int ROW_LEN = 20;
static int wrapInto(const char* t, int maxCh, char rows[][ROW_LEN], int maxRows, int count) {
  while (*t && count < maxRows) {
    int len = (int)strlen(t);
    if (len <= maxCh) {
      strncpy(rows[count], t, ROW_LEN - 1);
      rows[count][ROW_LEN - 1] = 0;
      return count + 1;
    }
    int cut = maxCh, skip = 0;
    bool found = false;
    for (int p = maxCh; p >= 1; p--) {
      if (t[p] == ' ') { cut = p; skip = 1; found = true; break; }
      if ((t[p - 1] == '-' || t[p - 1] == '.') && len - p >= 2) { cut = p; found = true; break; }
    }
    // A plain word or code with no separators (glow4821) splits evenly: glow|4821
    if (!found && len <= 2 * maxCh && !strpbrk(t, " -.")) cut = (len + 1) / 2;
    int n = cut < ROW_LEN - 1 ? cut : ROW_LEN - 1;
    memcpy(rows[count], t, n);
    rows[count][n] = 0;
    count++;
    t += cut + skip;
  }
  return count;
}

// Fonts from large to small: the first one that holds all the text is used.
struct TextTier { const uint8_t* font; int charW, base, pitch; };
static const TextTier TIERS[] = {
    {u8g2_font_8x13B_tr, 8, 10, 13},  // 9 chars x 3 rows (landscape), 5 x 5 (portrait)
    {u8g2_font_6x10_tr, 6, 8, 10},    // 12 x 4, 6 x 7
    {u8g2_font_5x7_tr, 5, 7, 9},      // 14 x 4, 8 x 8
    {u8g2_font_4x6_tr, 4, 6, 8},      // 18 x 5, 10 x 9
};

static void drawText(const char* const lines[4], bool portrait) {
  static char rows[12][ROW_LEN];
  const int W = portrait ? 40 : 72, H = portrait ? 72 : 40;
  const int nTiers = (int)(sizeof TIERS / sizeof TIERS[0]);
  int count = 0, used = nTiers - 1;
  for (int ti = 0; ti < nTiers; ti++) {
    const TextTier& tr = TIERS[ti];
    int maxCh = W / tr.charW, maxRows = H / tr.pitch;
    count = 0;
    for (int i = 0; i < 4; i++)
      if (lines[i] && *lines[i]) count = wrapInto(lines[i], maxCh, rows, 12, count);
    used = ti;
    if (count <= maxRows) break;
    if (ti == nTiers - 1) count = maxRows;  // nothing smaller: drop the overflow
  }
  u8g2.clearBuffer();
  u8g2.setFont(TIERS[used].font);
  for (int i = 0; i < count; i++) u8g2.drawStr(0, TIERS[used].base + TIERS[used].pitch * i, rows[i]);
  u8g2.sendBuffer();
}

// ---------------------------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------------------------
void Display::begin() {
  u8g2.begin();
  u8g2.setContrast(30);
  lastContrast_ = 30;
  u8g2.clearBuffer();
  u8g2.sendBuffer();
}

void Display::applySettings(const Settings& s) {
  if (rotationInit_ && s.rotation == rotation_) return;
  rotation_ = s.rotation;
  rotationInit_ = true;
  portrait_ = (rotation_ == 90 || rotation_ == 270);
  const u8g2_cb_t* cb = U8G2_R0;
  if (rotation_ == 90) cb = U8G2_R1;
  else if (rotation_ == 180) cb = U8G2_R2;
  else if (rotation_ == 270) cb = U8G2_R3;
  u8g2.setDisplayRotation(cb);
}

void Display::setContrastIfChanged(uint8_t c) {
  if (c != lastContrast_) {
    u8g2.setContrast(c);
    lastContrast_ = c;
  }
}

void Display::preview(State st, bool keepState, uint8_t contrast, uint16_t seconds) {
  previewState_ = st;
  previewKeep_ = keepState;
  previewContrast_ = contrast < 1 ? 1 : contrast;
  previewUntil_ = millis() + (uint32_t)seconds * 1000UL;
}

void Display::showNow(const char* l1, const char* l2, const char* l3, const char* l4) {
  setContrastIfChanged(40);
  const char* L[4] = {l1, l2, l3, l4};
  drawText(L, portrait_);
}

void Display::overlay(const char* l1, const char* l2, const char* l3, const char* l4,
                      uint32_t ms) {
  const char* src[4] = {l1, l2, l3, l4};
  for (int i = 0; i < 4; i++) copyStr(ov_[i], sizeof ov_[i], src[i]);
  ovUntil_ = millis() + ms;
  ovActive_ = true;
}

void Display::clearOverlay() { ovActive_ = false; }

bool Display::overlayActive() const { return ovActive_ && (int32_t)(ovUntil_ - millis()) > 0; }

void Display::drawArrow(int cx, int cy, int dir) {
  // dir -2..+2 -> -90..+90 degrees (0 = right, + = up)
  float a = dir * 45.0f * (float)M_PI / 180.0f;
  float dx = cosf(a), dy = -sinf(a);
  const float len = 7;
  int x1 = (int)lroundf(cx - dx * len), y1 = (int)lroundf(cy - dy * len);
  int x2 = (int)lroundf(cx + dx * len), y2 = (int)lroundf(cy + dy * len);
  u8g2.drawLine(x1, y1, x2, y2);
  u8g2.drawLine(x1, y1 + 1, x2, y2 + 1);
  for (int sgn = -1; sgn <= 1; sgn += 2) {
    float b = a + (float)M_PI + sgn * 0.65f;
    int hx = (int)lroundf(x2 + cosf(b) * 5), hy = (int)lroundf(y2 - sinf(b) * 5);
    u8g2.drawLine(x2, y2, hx, hy);
    u8g2.drawLine(x2, y2 + 1, hx, hy + 1);
  }
}

// Picks the largest value font whose text fits `maxW` pixels; returns the text width.
static int setValueFont(const char* text, int maxW) {
  static const uint8_t* const fonts[] = {u8g2_font_logisoso20_tr, u8g2_font_logisoso16_tr,
                                         u8g2_font_helvB12_tr, u8g2_font_6x10_tr};
  int w = 0;
  for (const uint8_t* f : fonts) {
    u8g2.setFont(f);
    w = u8g2.getStrWidth(text);
    if (w <= maxW) break;
  }
  return w;
}

// On/off phase for an animated element (an OLED pixel is on or off, so a "pulse" is a blink that
// stays on most of the time).
static bool elementPhase(AlertMode m, uint32_t now) {
  switch (m) {
    case ALERT_BLINK_SLOW: return ((now / 750) % 2) == 0;
    case ALERT_BLINK_FAST: return ((now / 150) % 2) == 0;
    case ALERT_PULSE: return (now % 2000) < 1400;      // slow pulse
    case ALERT_PULSE_FAST: return (now % 500) < 350;   // fast pulse
    case ALERT_TRIPLE: {
      uint32_t t = now % 2600;
      return t < 600 && (t % 200) < 100;
    }
    default: return true;
  }
}

void Display::drawView(const View& v, const Settings& s, int sx, int sy, bool elemOn) {
  u8g2.clearBuffer();
  const bool stale = v.state == State::Stale;
  const bool show = v.haveValue && !stale && v.state != State::Unknown;

  char buf[12];
  if (show) {
    if (s.units == 1) snprintf(buf, sizeof buf, "%.1f", mgdlToMmol(v.mgdl));
    else snprintf(buf, sizeof buf, "%d", v.mgdl);
  } else {
    snprintf(buf, sizeof buf, "--");
  }

  char age[12] = {0};
  if (s.showAge && v.ageKnown) {
    if (v.ageMin < 100) snprintf(age, sizeof age, "%dm", v.ageMin);
    else snprintf(age, sizeof age, "%dh", v.ageMin / 60);
  }
  const char* label = labelFor(v.state);
  const bool crit = v.state == State::CriticalLow || v.state == State::CriticalHigh;
  const bool arrowShown = show && s.showArrow && v.trendKnown;
  // Critical: the arrow animates (the label if there is no arrow). Stale: the STALE label.
  const bool drawArrowNow = arrowShown && (!crit || elemOn);
  const bool labelAnimated = (crit && !arrowShown) || stale;
  const bool drawLabelNow = *label && (!labelAnimated || elemOn);

  if (!portrait_) {
    // Landscape 72x40: value left, arrow right, age bottom-left, label bottom-right
    int vw = setValueFont(buf, 58);
    int vx = sx;
    if (vx + vw > 72) vx = 72 - vw;
    u8g2.drawStr(vx, 24 + sy, buf);
    if (stale) {
      u8g2.setFont(u8g2_font_6x10_tr);
      u8g2.drawStr(60 + sx, 12 + sy, "!");
    }
    if (drawArrowNow) drawArrow(62 + sx, 12 + sy, v.trendDir);
    u8g2.setFont(u8g2_font_5x7_tr);
    if (age[0]) u8g2.drawStr(sx, 39 + sy, age);
    if (drawLabelNow) u8g2.drawStr(72 - u8g2.getStrWidth(label) - 2 + sx, 39 + sy, label);
  } else {
    // Portrait 40x72: value on top, arrow below it, then the label, then the age
    int vw = setValueFont(buf, 40);
    int vx = (40 - vw) / 2 + sx;
    if (vx + vw > 40) vx = 40 - vw;
    if (vx < 0) vx = 0;
    u8g2.drawStr(vx, 22 + sy, buf);
    if (stale) {
      u8g2.setFont(u8g2_font_6x10_tr);
      u8g2.drawStr(17 + sx, 42 + sy, "!");
    }
    if (drawArrowNow) drawArrow(20 + sx, 38 + sy, v.trendDir);
    if (drawLabelNow) {
      u8g2.setFont(u8g2_font_5x7_tr);
      if (u8g2.getStrWidth(label) > 40) u8g2.setFont(u8g2_font_4x6_tr);
      int lw = u8g2.getStrWidth(label);
      u8g2.drawStr((40 - lw) / 2 + sx, 56 + sy, label);
    }
    if (age[0]) {
      u8g2.setFont(u8g2_font_5x7_tr);
      int aw = u8g2.getStrWidth(age);
      u8g2.drawStr((40 - aw) / 2 + sx, 68 + sy, age);
    }
  }
  u8g2.sendBuffer();
}

// Chart page: last 12 h scaled to the screen, with the normal range drawn as dotted guides.
void Display::drawChart(const Settings& s, int32_t nowS) {
  const int W = portrait_ ? 40 : 72, H = portrait_ ? 72 : 40;
  u8g2.clearBuffer();
  int lo = 40, hi = 240;
  for (int i = 0; i < hist_->size(); i++) {
    int m = hist_->at(i).mg;
    if (nowS - hist_->at(i).t > History::WINDOW_S) continue;
    if (m < lo) lo = m;
    if (m > hi) hi = m;
  }
  auto yOf = [&](int mg) { return (H - 1) - (mg - lo) * (H - 1) / (hi - lo); };
  for (int g : {(int)s.normalLow, (int)s.normalHigh}) {
    if (g < lo || g > hi) continue;
    int y = yOf(g);
    for (int x = 0; x < W; x += 4) u8g2.drawPixel(x, y);
  }
  int px = -1, py = -1;
  for (int i = 0; i < hist_->size(); i++) {
    const HistSample& h = hist_->at(i);
    int32_t age = nowS - h.t;
    if (age > History::WINDOW_S) continue;
    int x = (W - 1) - (int)((int64_t)age * (W - 1) / History::WINDOW_S);
    int y = yOf(h.mg);
    if (px >= 0) u8g2.drawLine(px, py, x, y);
    px = x;
    py = y;
  }
  if (px >= 0) {  // mark the newest reading
    u8g2.drawLine(px - 1, py, px + 1, py);
    u8g2.drawLine(px, py - 1, px, py + 1);
  }
  u8g2.sendBuffer();
}

void Display::update(const View& vIn, const Settings& s, bool timeValid, int minuteOfDay) {
  uint32_t now = millis();
  View v = vIn;
  bool previewing = (int32_t)(previewUntil_ - now) > 0;
  if (previewing && !previewKeep_) {
    v.state = previewState_;
    if (!v.haveValue) { v.haveValue = true; v.mgdl = 100; }
  }

  // Day/night level. Never zero: the display stays visible all night.
  // Until the clock is known (just booted, no internet) use the dim night level.
  night_ = useNightLevel(timeValid, minuteOfDay, s.dayStartMin, s.nightStartMin);
  uint8_t contrast = night_ ? s.nightContrast : s.dayContrast;
  if (previewing) {
    contrast = previewContrast_;
  }

  // Alert animation. Caution animates the whole screen; critical animates only the arrow and
  // stale only the STALE text (the number stays steady). Normal stays calm.
  const bool wholeScreen = v.state == State::CautionLow || v.state == State::CautionHigh;
  const AlertMode mode = modeFor(v.state, s);
  const bool elemOn = elementPhase(mode, now);
  bool contentOn = true;
  float scale = 1.0f;
  if (wholeScreen) switch (mode) {
    case ALERT_BLINK_SLOW: contentOn = ((now / 750) % 2) == 0; break;
    case ALERT_BLINK_FAST: contentOn = ((now / 150) % 2) == 0; break;
    case ALERT_PULSE:
      scale = 0.3f + 0.7f * (sinf(now / 1000.0f * 2.0f * (float)M_PI / 3.0f) + 1.0f) / 2.0f;
      break;
    case ALERT_PULSE_FAST:
      scale = 0.3f + 0.7f * (sinf(now / 1000.0f * 2.0f * (float)M_PI / 0.8f) + 1.0f) / 2.0f;
      break;
    case ALERT_TRIPLE: {
      uint32_t t = now % 2600;
      contentOn = t < 600 && (t % 200) < 100;
      break;
    }
    default: break;
  }

  // Text screens (boot hints, setup instructions) replace the glucose view; no animation on text
  if (overlayActive()) {
    setContrastIfChanged(contrast);
    const char* L[4] = {ov_[0], ov_[1], ov_[2], ov_[3]};
    drawText(L, portrait_);
    return;
  }

  int c = (int)lroundf(contrast * scale);
  if (c < 1) c = 1;
  setContrastIfChanged((uint8_t)c);
  if (!contentOn) {
    u8g2.clearBuffer();
    u8g2.sendBuffer();
    return;
  }
  // Chart page: 4 s out of every 12 s, only while glucose is normal/caution (alerts stay on the number)
  if (s.showChart && hist_ && !previewing && v.haveValue &&
      (v.state == State::Normal || v.state == State::CautionLow || v.state == State::CautionHigh) &&
      hist_->sizeInWindow((int32_t)(now / 1000)) >= 2 && (now / 1000) % 12 >= 8) {
    drawChart(s, (int32_t)(now / 1000));
    return;
  }
  int sx = 0, sy = 0;
  if (s.pixelShift) {
    uint32_t step = now / 300000UL;  // every 5 minutes
    sx = step % 3;
    sy = (step / 3) % 2;
  }
  drawView(v, s, sx, sy, elemOn);
}
