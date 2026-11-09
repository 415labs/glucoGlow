#pragma once
// Pure glucose logic: classification, colors, trend mapping, timestamp parsing, brightness schedule.
#include <stdint.h>
#include "settings.h"

// Zones: Critical (red) = CriticalLow/CriticalHigh, Caution (orange) = CautionLow/CautionHigh,
// Normal (green).
enum class State : uint8_t {
  Unknown = 0,
  CriticalLow,
  CautionLow,
  Normal,
  CautionHigh,
  CriticalHigh,
  Stale
};

const char* stateName(State s);
bool stateFromName(const char* name, State* out);

inline bool isCritical(State s) { return s == State::CriticalLow || s == State::CriticalHigh; }
inline bool isAlertState(State s) { return s != State::Unknown && s != State::Normal; }

// mg/dL value + staleness -> state, using the ranges in settings (defaults 60 / 90 / 150 / 200).
State classify(int mgdl, bool stale, const Settings& s);

// LibreLinkUp TrendArrow 1..5 (1 falling fast, 3 flat, 5 rising fast) -> -2..+2. False if unknown.
bool trendDir(int arrow, int* dir);

// Parses LibreLinkUp "M/D/YYYY h:mm:ss AM/PM" (FactoryTimestamp is UTC) to Unix epoch seconds.
bool parseLluTimestamp(const char* str, int64_t* epochOut);

// Minutes between reading and now; clamped to >= 0.
int ageMinutes(int64_t nowEpoch, int64_t readingEpoch);

// True if minuteOfDay falls in the night window. Day is [dayStart, nightStart), wrapping midnight.
bool isNight(int minuteOfDay, uint16_t dayStartMin, uint16_t nightStartMin);

// Display level: unknown clock => night (dim) level, so a freshly booted device never blazes at night.
bool useNightLevel(bool timeValid, int minuteOfDay, uint16_t dayStartMin, uint16_t nightStartMin);

// Display level: with an unknown clock use the dim night level, so a freshly booted device never
// blazes in a dark bedroom.
bool useNightLevel(bool timeValid, int minuteOfDay, uint16_t dayStartMin, uint16_t nightStartMin);

float mgdlToMmol(int mgdl);
