#include <unity.h>

#include "glucose.h"
#include "history.h"
#include "settings.h"

void setUp() {}
void tearDown() {}

static Settings defaults() {
  Settings s;
  settingsDefaults(s);
  return s;
}

// Default ranges: critical < 60, caution 60-89, normal 90-150, caution 151-200, critical > 200
void test_classify_default_ranges() {
  Settings s = defaults();
  TEST_ASSERT_EQUAL((int)State::CriticalLow, (int)classify(59, false, s));
  TEST_ASSERT_EQUAL((int)State::CriticalLow, (int)classify(40, false, s));
  TEST_ASSERT_EQUAL((int)State::CautionLow, (int)classify(60, false, s));
  TEST_ASSERT_EQUAL((int)State::CautionLow, (int)classify(89, false, s));
  TEST_ASSERT_EQUAL((int)State::Normal, (int)classify(90, false, s));
  TEST_ASSERT_EQUAL((int)State::Normal, (int)classify(120, false, s));
  TEST_ASSERT_EQUAL((int)State::Normal, (int)classify(150, false, s));
  TEST_ASSERT_EQUAL((int)State::CautionHigh, (int)classify(151, false, s));
  TEST_ASSERT_EQUAL((int)State::CautionHigh, (int)classify(200, false, s));
  TEST_ASSERT_EQUAL((int)State::CriticalHigh, (int)classify(201, false, s));
  TEST_ASSERT_EQUAL((int)State::CriticalHigh, (int)classify(400, false, s));
  TEST_ASSERT_EQUAL((int)State::Stale, (int)classify(100, true, s));
}

void test_classify_follows_custom_ranges() {
  Settings s = defaults();
  s.criticalLow = 70;
  s.normalLow = 80;
  s.normalHigh = 140;
  s.criticalHigh = 180;
  settingsClamp(s);
  TEST_ASSERT_EQUAL((int)State::CriticalLow, (int)classify(69, false, s));
  TEST_ASSERT_EQUAL((int)State::CautionLow, (int)classify(70, false, s));
  TEST_ASSERT_EQUAL((int)State::Normal, (int)classify(80, false, s));
  TEST_ASSERT_EQUAL((int)State::Normal, (int)classify(140, false, s));
  TEST_ASSERT_EQUAL((int)State::CautionHigh, (int)classify(141, false, s));
  TEST_ASSERT_EQUAL((int)State::CriticalHigh, (int)classify(181, false, s));
}

void test_colors_per_zone() {
  Rgb r = colorFor(State::CriticalLow);
  TEST_ASSERT_EQUAL(255, r.r);
  TEST_ASSERT_EQUAL(0, r.g);
  TEST_ASSERT_EQUAL(0, r.b);
  r = colorFor(State::CriticalHigh);  // both critical states are red
  TEST_ASSERT_EQUAL(255, r.r);
  TEST_ASSERT_EQUAL(0, r.g);
  r = colorFor(State::Normal);  // green
  TEST_ASSERT_EQUAL(0, r.r);
  TEST_ASSERT_EQUAL(255, r.g);
  TEST_ASSERT_EQUAL(0, r.b);
  Rgb lo = colorFor(State::CautionLow);  // orange: lots of red, some green, no blue
  Rgb hi = colorFor(State::CautionHigh);
  TEST_ASSERT_EQUAL(255, lo.r);
  TEST_ASSERT_TRUE(lo.g > 0 && lo.g < 200);
  TEST_ASSERT_EQUAL(0, lo.b);
  TEST_ASSERT_EQUAL(lo.r, hi.r);
  TEST_ASSERT_EQUAL(lo.g, hi.g);
  Rgb u = colorFor(State::Unknown);
  TEST_ASSERT_EQUAL(0, u.r + u.g + u.b);
}

void test_state_helpers() {
  TEST_ASSERT_TRUE(isCritical(State::CriticalLow));
  TEST_ASSERT_TRUE(isCritical(State::CriticalHigh));
  TEST_ASSERT_FALSE(isCritical(State::CautionLow));
  TEST_ASSERT_FALSE(isAlertState(State::Normal));
  TEST_ASSERT_FALSE(isAlertState(State::Unknown));
  TEST_ASSERT_TRUE(isAlertState(State::CautionHigh));
  TEST_ASSERT_TRUE(isAlertState(State::Stale));
}

void test_trend_dir() {
  int d = 99;
  TEST_ASSERT_TRUE(trendDir(1, &d));
  TEST_ASSERT_EQUAL(-2, d);
  TEST_ASSERT_TRUE(trendDir(3, &d));
  TEST_ASSERT_EQUAL(0, d);
  TEST_ASSERT_TRUE(trendDir(5, &d));
  TEST_ASSERT_EQUAL(2, d);
  TEST_ASSERT_FALSE(trendDir(0, &d));
  TEST_ASSERT_FALSE(trendDir(6, &d));
}

void test_parse_timestamp() {
  int64_t t = -1;
  TEST_ASSERT_TRUE(parseLluTimestamp("1/1/1970 12:00:00 AM", &t));
  TEST_ASSERT_EQUAL_INT64(0, t);
  TEST_ASSERT_TRUE(parseLluTimestamp("1/1/1970 12:00:00 PM", &t));
  TEST_ASSERT_EQUAL_INT64(43200, t);
  TEST_ASSERT_TRUE(parseLluTimestamp("1/2/1970 1:30:05 AM", &t));
  TEST_ASSERT_EQUAL_INT64(86400 + 3600 + 30 * 60 + 5, t);
  // 2026-11-08 20:27:00 UTC
  TEST_ASSERT_TRUE(parseLluTimestamp("11/8/2026 8:27:00 PM", &t));
  TEST_ASSERT_EQUAL_INT64(1794169620LL, t);
  // leap day
  TEST_ASSERT_TRUE(parseLluTimestamp("3/1/2024 12:00:00 AM", &t));
  TEST_ASSERT_EQUAL_INT64(1709251200LL, t);
}

void test_parse_timestamp_rejects_garbage() {
  int64_t t;
  TEST_ASSERT_FALSE(parseLluTimestamp("", &t));
  TEST_ASSERT_FALSE(parseLluTimestamp(nullptr, &t));
  TEST_ASSERT_FALSE(parseLluTimestamp("2026-11-08T20:27:00Z", &t));
  TEST_ASSERT_FALSE(parseLluTimestamp("13/8/2026 8:27:00 PM", &t));
  TEST_ASSERT_FALSE(parseLluTimestamp("11/8/2026 8:27:00 XM", &t));
}

void test_age_minutes() {
  TEST_ASSERT_EQUAL(0, ageMinutes(1000, 1000));
  TEST_ASSERT_EQUAL(1, ageMinutes(1060, 1000));
  TEST_ASSERT_EQUAL(15, ageMinutes(1000 + 15 * 60 + 59, 1000));
  TEST_ASSERT_EQUAL(0, ageMinutes(1000, 2000));  // reading "from the future" clamps to 0
}

void test_is_night_normal_window() {
  // day 07:00-22:00
  TEST_ASSERT_FALSE(isNight(7 * 60, 7 * 60, 22 * 60));
  TEST_ASSERT_FALSE(isNight(12 * 60, 7 * 60, 22 * 60));
  TEST_ASSERT_FALSE(isNight(22 * 60 - 1, 7 * 60, 22 * 60));
  TEST_ASSERT_TRUE(isNight(22 * 60, 7 * 60, 22 * 60));
  TEST_ASSERT_TRUE(isNight(0, 7 * 60, 22 * 60));
  TEST_ASSERT_TRUE(isNight(7 * 60 - 1, 7 * 60, 22 * 60));
}

void test_is_night_wrapping_day_window() {
  // day 22:00 -> 07:00 (night shift), night 07:00-22:00
  TEST_ASSERT_FALSE(isNight(23 * 60, 22 * 60, 7 * 60));
  TEST_ASSERT_FALSE(isNight(3 * 60, 22 * 60, 7 * 60));
  TEST_ASSERT_TRUE(isNight(12 * 60, 22 * 60, 7 * 60));
  TEST_ASSERT_TRUE(isNight(7 * 60, 22 * 60, 7 * 60));
  TEST_ASSERT_FALSE(isNight(22 * 60, 22 * 60, 7 * 60));
}

void test_is_night_equal_means_never() {
  TEST_ASSERT_FALSE(isNight(0, 420, 420));
  TEST_ASSERT_FALSE(isNight(700, 420, 420));
}

void test_default_settings() {
  Settings s = defaults();
  TEST_ASSERT_EQUAL(60, s.pollSec);
  TEST_ASSERT_EQUAL(60, s.criticalLow);
  TEST_ASSERT_EQUAL(90, s.normalLow);
  TEST_ASSERT_EQUAL(150, s.normalHigh);
  TEST_ASSERT_EQUAL(200, s.criticalHigh);
  TEST_ASSERT_EQUAL(-1, s.ledPin);
  TEST_ASSERT_TRUE(s.nightContrast >= 1);  // night is dim but visible
  TEST_ASSERT_TRUE(s.dayContrast > s.nightContrast);
  TEST_ASSERT_EQUAL_STRING("4.16.0", s.lluVersion);
}

void test_defaults_stable_under_clamp() {
  Settings s = defaults();
  Settings c = s;
  settingsClamp(c);
  TEST_ASSERT_EQUAL(0, memcmp(&s, &c, sizeof s));
}

void test_clamp_poll_interval() {
  Settings s = defaults();
  s.pollSec = 5;
  settingsClamp(s);
  TEST_ASSERT_EQUAL(30, s.pollSec);
  s.pollSec = 60000;
  settingsClamp(s);
  TEST_ASSERT_EQUAL(900, s.pollSec);
}

void test_clamp_keeps_ranges_ordered() {
  Settings s = defaults();
  s.criticalLow = 120;   // out of range and above everything else
  s.normalLow = 80;
  s.normalHigh = 70;
  s.criticalHigh = 60;
  settingsClamp(s);
  TEST_ASSERT_TRUE(s.criticalLow < s.normalLow);
  TEST_ASSERT_TRUE(s.normalLow <= s.normalHigh);
  TEST_ASSERT_TRUE(s.normalHigh < s.criticalHigh);
  TEST_ASSERT_TRUE(s.criticalLow >= 30 && s.criticalLow <= 100);
}

void test_clamp_brightness_never_dark() {
  Settings s = defaults();
  s.dayContrast = 0;
  s.nightContrast = 0;
  s.ledDay = 0;
  s.ledNight = 0;
  settingsClamp(s);
  TEST_ASSERT_EQUAL(1, s.dayContrast);
  TEST_ASSERT_EQUAL(1, s.nightContrast);
  TEST_ASSERT_EQUAL(1, s.ledDay);
  TEST_ASSERT_EQUAL(1, s.ledNight);
}

void test_clamp_misc() {
  Settings s = defaults();
  s.dayStartMin = 1500;
  s.nightStartMin = 2900;
  s.criticalLowMode = 99;
  s.cautionMode = 99;
  s.units = 7;
  settingsClamp(s);
  TEST_ASSERT_TRUE(s.dayStartMin < 1440);
  TEST_ASSERT_TRUE(s.nightStartMin < 1440);
  TEST_ASSERT_EQUAL(ALERT_BLINK_FAST, s.criticalLowMode);
  TEST_ASSERT_EQUAL(ALERT_PULSE, s.cautionMode);
  TEST_ASSERT_EQUAL(1, s.units);
}

void test_led_pin_validation() {
  Settings s = defaults();
  s.ledPin = 4;
  settingsClamp(s);
  TEST_ASSERT_EQUAL(4, s.ledPin);
  s.ledPin = 5;  // OLED SDA
  settingsClamp(s);
  TEST_ASSERT_EQUAL(-1, s.ledPin);
  s.ledPin = 9;  // BOOT button
  settingsClamp(s);
  TEST_ASSERT_EQUAL(-1, s.ledPin);
  s.ledPin = 8;  // onboard LED
  settingsClamp(s);
  TEST_ASSERT_EQUAL(-1, s.ledPin);
  s.ledPin = 77;
  settingsClamp(s);
  TEST_ASSERT_EQUAL(-1, s.ledPin);
}

void test_ap_password_rules() {
  Settings s = defaults();
  copyStr(s.apPass, sizeof s.apPass, "12345678");
  settingsClamp(s);
  TEST_ASSERT_EQUAL_STRING("12345678", s.apPass);
  copyStr(s.apPass, sizeof s.apPass, "short");  // too short for WPA2: cleared (=> regenerated)
  settingsClamp(s);
  TEST_ASSERT_EQUAL_STRING("", s.apPass);
}

void test_lluversion_default_when_empty() {
  Settings s = defaults();
  s.lluVersion[0] = 0;
  settingsClamp(s);
  TEST_ASSERT_EQUAL_STRING("4.16.0", s.lluVersion);
}

void test_hostname_sanitized() {
  Settings s = defaults();
  copyStr(s.hostname, sizeof s.hostname, "My Gluco_LED!!");
  settingsClamp(s);
  TEST_ASSERT_EQUAL_STRING("myglucoled", s.hostname);
  copyStr(s.hostname, sizeof s.hostname, "!!!");
  settingsClamp(s);
  TEST_ASSERT_EQUAL_STRING("glucoglow", s.hostname);
}

void test_rotation_setting() {
  Settings s;
  settingsDefaults(s);
  TEST_ASSERT_EQUAL(0, s.rotation);
  const int ok[] = {0, 90, 180, 270};
  for (int r : ok) {
    s.rotation = (uint16_t)r;
    settingsClamp(s);
    TEST_ASSERT_EQUAL(r, s.rotation);
  }
  const int bad[] = {45, 91, 360, 1, 65535};
  for (int r : bad) {
    s.rotation = (uint16_t)r;
    settingsClamp(s);
    TEST_ASSERT_EQUAL(0, s.rotation);
  }
}

void test_history_spacing_and_wrap() {
  History h;
  h.add(1000, 100);
  h.add(1100, 101);  // too soon: ignored
  h.add(1300, 102);
  TEST_ASSERT_EQUAL(2, h.size());
  TEST_ASSERT_EQUAL(100, h.at(0).mg);
  TEST_ASSERT_EQUAL(102, h.at(1).mg);
  for (int i = 0; i < 200; i++) h.add(2000 + i * 300, (uint16_t)(i + 1));
  TEST_ASSERT_EQUAL(History::CAP, h.size());
  TEST_ASSERT_EQUAL(200, h.at(h.size() - 1).mg);
  TEST_ASSERT_EQUAL(200 - History::CAP + 1, h.at(0).mg);
  uint32_t now = 2000 + 199 * 300;
  TEST_ASSERT_EQUAL(12 * 12 + 1, h.sizeInWindow(now));
}

void test_history_backfill_negative_times() {
  History h;
  h.add(-44000, 90);  // before this boot, older than 12 h
  h.add(-39000, 95);
  h.add(-38900, 96);  // <5 min after the previous one: skipped
  h.add(20, 100);
  TEST_ASSERT_EQUAL(3, h.size());
  TEST_ASSERT_EQUAL(2, h.sizeInWindow(20));  // -44000 is older than 12 h
}

void test_chart_setting_default_on() {
  Settings s;
  settingsDefaults(s);
  TEST_ASSERT_TRUE(s.showChart);
}

void test_state_names_roundtrip() {
  State out;
  TEST_ASSERT_TRUE(stateFromName("critical_low", &out));
  TEST_ASSERT_EQUAL((int)State::CriticalLow, (int)out);
  TEST_ASSERT_TRUE(stateFromName("caution_high", &out));
  TEST_ASSERT_EQUAL((int)State::CautionHigh, (int)out);
  TEST_ASSERT_TRUE(stateFromName(stateName(State::Stale), &out));
  TEST_ASSERT_EQUAL((int)State::Stale, (int)out);
  TEST_ASSERT_FALSE(stateFromName("nope", &out));
  TEST_ASSERT_FALSE(stateFromName(nullptr, &out));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_classify_default_ranges);
  RUN_TEST(test_classify_follows_custom_ranges);
  RUN_TEST(test_colors_per_zone);
  RUN_TEST(test_state_helpers);
  RUN_TEST(test_trend_dir);
  RUN_TEST(test_parse_timestamp);
  RUN_TEST(test_parse_timestamp_rejects_garbage);
  RUN_TEST(test_age_minutes);
  RUN_TEST(test_is_night_normal_window);
  RUN_TEST(test_is_night_wrapping_day_window);
  RUN_TEST(test_is_night_equal_means_never);
  RUN_TEST(test_default_settings);
  RUN_TEST(test_defaults_stable_under_clamp);
  RUN_TEST(test_clamp_poll_interval);
  RUN_TEST(test_clamp_keeps_ranges_ordered);
  RUN_TEST(test_clamp_brightness_never_dark);
  RUN_TEST(test_clamp_misc);
  RUN_TEST(test_led_pin_validation);
  RUN_TEST(test_ap_password_rules);
  RUN_TEST(test_lluversion_default_when_empty);
  RUN_TEST(test_hostname_sanitized);
  RUN_TEST(test_rotation_setting);
  RUN_TEST(test_history_spacing_and_wrap);
  RUN_TEST(test_history_backfill_negative_times);
  RUN_TEST(test_chart_setting_default_on);
  RUN_TEST(test_state_names_roundtrip);
  return UNITY_END();
}
