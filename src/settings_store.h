#pragma once
// NVS persistence + JSON (de)serialization of Settings, shared by the web API.
#include <ArduinoJson.h>
#include "settings.h"

void settingsLoad(Settings& s);
void settingsSave(const Settings& s);
void settingsFactoryReset();

// Generates a random 8-digit setup-WiFi password if none is set. Returns true if it changed.
bool settingsEnsureApPass(Settings& s);

// includeSecrets=false is used for the web API: secrets are never sent to the browser.
void settingsToJson(const Settings& s, JsonDocument& d, bool includeSecrets);

// Applies only the keys present in `d`. Empty secret strings are ignored (= "unchanged").
// Does NOT clamp; call settingsClamp afterwards.
void settingsApplyJson(JsonVariantConst d, Settings& s);
