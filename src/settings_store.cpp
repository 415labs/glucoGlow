#include "settings_store.h"

#include <Preferences.h>
#include <esp_system.h>

static const char* NS = "glucoled";  // NVS namespace: kept from the old name so saved settings survive the rename
static const char* KEY = "cfg";

static void setStr(JsonVariantConst v, char* dst, size_t n, bool secret) {
  if (!v.is<const char*>()) return;
  const char* x = v.as<const char*>();
  if (secret && !*x) return;
  copyStr(dst, n, x);
}
template <typename T>
static void setNum(JsonVariantConst v, T& dst) {
  if (v.is<int>()) dst = (T)v.as<int>();
}
static void setBool(JsonVariantConst v, bool& dst) {
  if (v.is<bool>()) dst = v.as<bool>();
}

void settingsToJson(const Settings& s, JsonDocument& d, bool secrets) {
  d["wifiSsid"] = s.wifiSsid;
  d["lluEmail"] = s.lluEmail;
  d["lluRegion"] = s.lluRegion;
  d["lluVersion"] = s.lluVersion;
  d["tlsInsecure"] = s.tlsInsecure;
  d["pollSec"] = s.pollSec;
  d["units"] = s.units;
  d["criticalLow"] = s.criticalLow;
  d["normalLow"] = s.normalLow;
  d["normalHigh"] = s.normalHigh;
  d["criticalHigh"] = s.criticalHigh;
  d["staleMin"] = s.staleMin;
  d["criticalLowMode"] = s.criticalLowMode;
  d["criticalHighMode"] = s.criticalHighMode;
  d["cautionMode"] = s.cautionMode;
  d["staleMode"] = s.staleMode;
  d["dayStartMin"] = s.dayStartMin;
  d["nightStartMin"] = s.nightStartMin;
  d["dayContrast"] = s.dayContrast;
  d["nightContrast"] = s.nightContrast;
  d["tz"] = s.tz;
  d["showArrow"] = s.showArrow;
  d["showAge"] = s.showAge;
  d["pixelShift"] = s.pixelShift;
  d["showChart"] = s.showChart;
  d["rotation"] = s.rotation;
  d["hostname"] = s.hostname;
  if (secrets) {
    d["wifiPass"] = s.wifiPass;
    d["lluPass"] = s.lluPass;
    d["adminPass"] = s.adminPass;
    d["apPass"] = s.apPass;
  } else {
    // Only tell the browser whether a secret exists.
    d["wifiPassSet"] = s.wifiPass[0] != 0;
    d["lluPassSet"] = s.lluPass[0] != 0;
    d["adminPassSet"] = s.adminPass[0] != 0;
    d["apPassSet"] = s.apPass[0] != 0;
  }
}

void settingsApplyJson(JsonVariantConst d, Settings& s) {
  setStr(d["wifiSsid"], s.wifiSsid, sizeof s.wifiSsid, false);
  setStr(d["wifiPass"], s.wifiPass, sizeof s.wifiPass, true);
  setStr(d["lluEmail"], s.lluEmail, sizeof s.lluEmail, false);
  setStr(d["lluPass"], s.lluPass, sizeof s.lluPass, true);
  setStr(d["lluRegion"], s.lluRegion, sizeof s.lluRegion, false);
  setStr(d["lluVersion"], s.lluVersion, sizeof s.lluVersion, true);
  setBool(d["tlsInsecure"], s.tlsInsecure);
  setNum(d["pollSec"], s.pollSec);
  setNum(d["units"], s.units);
  setNum(d["criticalLow"], s.criticalLow);
  setNum(d["normalLow"], s.normalLow);
  setNum(d["normalHigh"], s.normalHigh);
  setNum(d["criticalHigh"], s.criticalHigh);
  setNum(d["staleMin"], s.staleMin);
  setNum(d["criticalLowMode"], s.criticalLowMode);
  setNum(d["criticalHighMode"], s.criticalHighMode);
  setNum(d["cautionMode"], s.cautionMode);
  setNum(d["staleMode"], s.staleMode);
  setNum(d["dayStartMin"], s.dayStartMin);
  setNum(d["nightStartMin"], s.nightStartMin);
  setNum(d["dayContrast"], s.dayContrast);
  setNum(d["nightContrast"], s.nightContrast);
  setStr(d["tz"], s.tz, sizeof s.tz, false);
  setBool(d["showArrow"], s.showArrow);
  setBool(d["showAge"], s.showAge);
  setBool(d["pixelShift"], s.pixelShift);
  setBool(d["showChart"], s.showChart);
  setNum(d["rotation"], s.rotation);
  setStr(d["hostname"], s.hostname, sizeof s.hostname, false);
  setStr(d["adminPass"], s.adminPass, sizeof s.adminPass, true);
  setStr(d["apPass"], s.apPass, sizeof s.apPass, true);
  if (d["clearAdminPass"].is<bool>() && d["clearAdminPass"].as<bool>()) s.adminPass[0] = 0;
}

// Setup WiFi password: "glow" + 4 random digits (WPA2 needs at least 8 characters).
// A password you set yourself is kept; the old 8-digit generated ones are replaced.
bool settingsEnsureApPass(Settings& s) {
  size_t n = strlen(s.apPass);
  bool oldGenerated = n == 8;
  for (size_t i = 0; oldGenerated && i < n; i++) oldGenerated = s.apPass[i] >= '0' && s.apPass[i] <= '9';
  if (n >= 8 && !oldGenerated) return false;
  snprintf(s.apPass, sizeof s.apPass, "glow%04lu", (unsigned long)(esp_random() % 10000UL));
  return true;
}

void settingsLoad(Settings& s) {
  settingsDefaults(s);
  Preferences p;
  if (p.begin(NS, true)) {
    String json = p.getString(KEY, "");
    p.end();
    if (json.length()) {
      JsonDocument d;
      if (!deserializeJson(d, json)) settingsApplyJson(d, s);
    }
  }
  settingsClamp(s);
}

void settingsSave(const Settings& s) {
  JsonDocument d;
  settingsToJson(s, d, true);
  String json;
  serializeJson(d, json);
  Preferences p;
  if (p.begin(NS, false)) {
    p.putString(KEY, json);
    p.end();
  }
}

void settingsFactoryReset() {
  Preferences p;
  if (p.begin(NS, false)) {
    p.clear();
    p.end();
  }
}
