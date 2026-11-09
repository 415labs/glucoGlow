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
// Text screens
// ---------------------------------------------------------------------------------------------

// Landscape (72x40): up to 4 text rows (baselines 8, 18, 28, 38). Rows too wide for the panel use a
// smaller font. A leading '\x01' asks for the larger 6x10 font when the row fits.
static void drawTextLandscape(const char* const lines[4]) {
  u8g2.clearBuffer();
  for (int i = 0; i < 4; i++) {
    const char* t = lines[i];
    if (!t || !*t) continue;
    bool big = (t[0] == '\x01');
    if (big) t++;
    size_t n = strlen(t);
    if (big && n * 6 <= 72) u8g2.setFont(u8g2_font_6x10_tr);
    else u8g2.setFont(n * 5 <= 72 ? u8g2_font_5x7_tr : u8g2_font_4x6_tr);
    u8g2.drawStr(0, 8 + 10 * i + (big ? 1 : 0), t);
  }
  u8g2.sendBuffer();
}

// Splits `t` into rows of at most maxCh characters, preferring to break at a space (dropped) or
// right after '-' / '.'. Appends to rows[], returns the new row count.
static int wrapInto(const char* t, int maxCh, char rows[][12], int maxRows, int count) {
  while (*t && count < maxRows) {
    int len = (int)strlen(t);
    if (len <= maxCh) {
      strncpy(rows[count], t, 11);
      rows[count][11] = 0;
      return count + 1;
    }
    int cut = maxCh, skip = 0;
    for (int p = maxCh; p >= 1; p--) {
      if (t[p] == ' ') { cut = p; skip = 1; break; }
      if (t[p - 1] == '-' || t[p - 1] == '.') { cut = p; break; }
    }
    int n = cut < 11 ? cut : 11 - 1;
    memcpy(rows[count], t, n);
    rows[count][n] = 0;
    count++;
    t += cut + skip;
  }
  return count;
}

// Portrait (40x72): every line is wrapped to the narrow width. Uses the 5x7 font (8 chars, 8 rows)
// when everything fits, otherwise the 4x6 font (10 chars, 9 rows).
static void drawTextPortrait(const char* const lines[4]) {
  static char rows[10][12];
  struct Pass { const uint8_t* font; int maxCh, pitch, base, maxRows; };
  const Pass passes[2] = {{u8g2_font_5x7_tr, 8, 9, 7, 8}, {u8g2_font_4x6_tr, 10, 8, 6, 9}};
  int count = 0, used = 1;
  for (int p = 0; p < 2; p++) {
    count = 0;
    bool fits = true;
    for (int i = 0; i < 4; i++) {
      const char* t = lines[i];
      if (!t || !*t) continue;
      if (t[0] == '\x01') t++;  // the large font does not fit 40 px
      int before = count;
      count = wrapInto(t, passes[p].maxCh, rows, passes[p].maxRows, count);
      if (count - before == 0 || (count == passes[p].maxRows && (int)strlen(t) > passes[p].maxCh * (count - before)))
        fits = false;
    }
    used = p;
    if (fits || p == 1) break;
  }
  u8g2.clearBuffer();
  u8g2.setFont(passes[used].font);
  for (int i = 0; i < count; i++) u8g2.drawStr(0, passes[used].base + passes[used].pitch * i, rows[i]);
  u8g2.sendBuffer();
}

static void drawText(const char* const lines[4], bool portrait) {
  if (portrait) drawTextPortrait(lines);
  else drawTextLandscape(lines);
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
  led_.begin(s.ledPin);
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

void Display::preview(State st, bool keepState, uint8_t contrast, uint8_t ledBrightness,
                      uint16_t seconds) {
  previewState_ = st;
  previewKeep_ = keepState;
  previewContrast_ = contrast < 1 ? 1 : contrast;
  previewLed_ = ledBrightness < 1 ? 1 : ledBrightness;
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

void Display::drawView(const View& v, const Settings& s, int sx, int sy) {
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
    if (show && s.showArrow && v.trendKnown) drawArrow(62 + sx, 12 + sy, v.trendDir);
    u8g2.setFont(u8g2_font_5x7_tr);
    if (age[0]) u8g2.drawStr(sx, 39 + sy, age);
    if (*label) u8g2.drawStr(72 - u8g2.getStrWidth(label) - 2 + sx, 39 + sy, label);
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
    if (show && s.showArrow && v.trendKnown) drawArrow(20 + sx, 38 + sy, v.trendDir);
    if (*label) {
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

  // Day/night level. Never zero: the display and LED stay visible all night.
  night_ = timeValid && ::isNight(minuteOfDay, s.dayStartMin, s.nightStartMin);
  uint8_t contrast = night_ ? s.nightContrast : s.dayContrast;
  uint8_t ledLevel = night_ ? s.ledNight : s.ledDay;
  if (previewing) {
    contrast = previewContrast_;
    ledLevel = previewLed_;
  }

  // Alert animation (only for non-normal states; normal stays calm)
  bool contentOn = true;
  float scale = 1.0f;
  switch (modeFor(v.state, s)) {
    case ALERT_BLINK_SLOW: contentOn = ((now / 750) % 2) == 0; break;
    case ALERT_BLINK_FAST: contentOn = ((now / 150) % 2) == 0; break;
    case ALERT_PULSE:
      scale = 0.3f + 0.7f * (sinf(now / 1000.0f * 2.0f * (float)M_PI / 3.0f) + 1.0f) / 2.0f;
      break;
    case ALERT_TRIPLE: {
      uint32_t t = now % 2600;
      contentOn = t < 600 && (t % 200) < 100;
      break;
    }
    default: break;
  }

  // Optional RGB LED follows the same color / animation / schedule
  Rgb col = colorFor(v.state);
  float k = contentOn ? (ledLevel / 255.0f) * scale : 0.0f;
  led_.set((uint8_t)lroundf(col.r * k), (uint8_t)lroundf(col.g * k), (uint8_t)lroundf(col.b * k));

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
  drawView(v, s, sx, sy);
}
