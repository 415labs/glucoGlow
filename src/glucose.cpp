#include "glucose.h"

#include <stdio.h>
#include <string.h>

const char* stateName(State s) {
  switch (s) {
    case State::CriticalLow: return "critical_low";
    case State::CautionLow: return "caution_low";
    case State::Normal: return "normal";
    case State::CautionHigh: return "caution_high";
    case State::CriticalHigh: return "critical_high";
    case State::Stale: return "stale";
    default: return "unknown";
  }
}

bool stateFromName(const char* name, State* out) {
  static const State all[] = {State::Unknown,     State::CriticalLow,  State::CautionLow,
                              State::Normal,      State::CautionHigh,  State::CriticalHigh,
                              State::Stale};
  if (!name) return false;
  for (State st : all) {
    if (strcmp(name, stateName(st)) == 0) {
      *out = st;
      return true;
    }
  }
  return false;
}

State classify(int mgdl, bool stale, const Settings& s) {
  if (stale) return State::Stale;
  if (mgdl < (int)s.criticalLow) return State::CriticalLow;
  if (mgdl < (int)s.normalLow) return State::CautionLow;
  if (mgdl <= (int)s.normalHigh) return State::Normal;
  if (mgdl <= (int)s.criticalHigh) return State::CautionHigh;
  return State::CriticalHigh;
}

Rgb colorFor(State s) {
  switch (s) {
    case State::CriticalLow:
    case State::CriticalHigh: return {255, 0, 0};    // red
    case State::CautionLow:
    case State::CautionHigh: return {255, 100, 0};   // orange
    case State::Normal: return {0, 255, 0};          // green
    case State::Stale: return {0, 0, 255};           // blue = no fresh data
    default: return {0, 0, 0};                       // unknown: off
  }
}

bool trendDir(int arrow, int* dir) {
  if (arrow < 1 || arrow > 5) return false;
  *dir = arrow - 3;
  return true;
}

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm).
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

bool parseLluTimestamp(const char* str, int64_t* epochOut) {
  if (!str) return false;
  int mo, d, y, h, mi, se;
  char ap[3] = {0};
  if (sscanf(str, "%d/%d/%d %d:%d:%d %2s", &mo, &d, &y, &h, &mi, &se, ap) != 7) return false;
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || y < 1970 || h < 1 || h > 12 || mi < 0 || mi > 59 ||
      se < 0 || se > 60)
    return false;
  bool pm;
  if (ap[0] == 'P' || ap[0] == 'p') pm = true;
  else if (ap[0] == 'A' || ap[0] == 'a') pm = false;
  else return false;
  h = (h % 12) + (pm ? 12 : 0);
  *epochOut = daysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + se;
  return true;
}

int ageMinutes(int64_t nowEpoch, int64_t readingEpoch) {
  int64_t diff = nowEpoch - readingEpoch;
  if (diff < 0) diff = 0;
  return (int)(diff / 60);
}

bool isNight(int minuteOfDay, uint16_t dayStartMin, uint16_t nightStartMin) {
  if (dayStartMin == nightStartMin) return false;  // no night window
  if (dayStartMin < nightStartMin) {
    return !(minuteOfDay >= dayStartMin && minuteOfDay < nightStartMin);
  }
  // day window wraps midnight (e.g. day 22:00 -> 07:00)
  return minuteOfDay >= nightStartMin && minuteOfDay < dayStartMin;
}

float mgdlToMmol(int mgdl) { return mgdl / 18.016f; }
