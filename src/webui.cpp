#include "webui.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "llu_client.h"
#include "settings_store.h"
#include "webui_html.h"

static WebServer server(80);
static DNSServer dns;
static Settings* gSet = nullptr;
static const DeviceStatus* gSt = nullptr;
static const History* gHist = nullptr;
static WebUiHooks gHooks;
static bool gAp = false;  // setup WiFi (access point) on

// True when the request arrived through the setup WiFi (its password already authenticated it).
static bool viaSetupAp() { return gAp && server.client().localIP() == WiFi.softAPIP(); }

// Over your normal WiFi an optional admin password protects everything.
static bool authOk() {
  if (!gSet->adminPass[0] || viaSetupAp()) return true;
  if (server.authenticate("admin", gSet->adminPass)) return true;
  server.requestAuthentication();
  return false;
}

static void sendJson(JsonDocument& d, int code = 200) {
  String out;
  serializeJson(d, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", out);
}

static void sendError(const char* msg, int code = 400) {
  JsonDocument d;
  d["error"] = msg;
  sendJson(d, code);
}

static bool readBody(JsonDocument& d) { return !deserializeJson(d, server.arg("plain")); }

static void handleRoot() {
  if (!authOk()) return;
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", INDEX_HTML);
}

static void handleGetSettings() {
  if (!authOk()) return;
  JsonDocument d;
  settingsToJson(*gSet, d, false);
  sendJson(d);
}

static void handlePostSettings() {
  if (!authOk()) return;
  JsonDocument in;
  if (!readBody(in)) return sendError("invalid JSON");

  // The setup WiFi password must be usable for WPA2; reject instead of silently replacing it.
  if (in["apPass"].is<const char*>()) {
    size_t n = strlen(in["apPass"].as<const char*>());
    if (n != 0 && (n < 8 || n > 32)) return sendError("Setup WiFi password must be 8-32 characters");
  }

  Settings next = *gSet;
  settingsApplyJson(in, next);
  settingsClamp(next);
  settingsEnsureApPass(next);
  bool rebootNeeded = strcmp(next.wifiSsid, gSet->wifiSsid) != 0 ||
                      strcmp(next.wifiPass, gSet->wifiPass) != 0 ||
                      strcmp(next.hostname, gSet->hostname) != 0;
  *gSet = next;
  settingsSave(*gSet);
  if (gHooks.onSettingsSaved) gHooks.onSettingsSaved();
  JsonDocument out;
  out["ok"] = true;
  out["rebootRequired"] = rebootNeeded;
  sendJson(out);
}

static void handleStatus() {
  if (!authOk()) return;
  const DeviceStatus& s = *gSt;
  bool sta = WiFi.status() == WL_CONNECTED;
  JsonDocument d;
  d["version"] = FW_VERSION;
  d["uptimeS"] = millis() / 1000;
  d["freeHeap"] = ESP.getFreeHeap();
  d["setupAp"] = gAp;
  d["provisioning"] = s.provisioning;
  d["apSsid"] = AP_SSID;
  d["stations"] = gAp ? WiFi.softAPgetStationNum() : 0;
  d["hostname"] = gSet->hostname;
  d["ip"] = sta ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  d["rssi"] = sta ? WiFi.RSSI() : 0;
  d["timeValid"] = s.timeValid;
  d["night"] = s.night;
  d["state"] = stateName(s.state);
  d["haveValue"] = s.haveValue;
  d["mgdl"] = s.mgdl;
  d["trendKnown"] = s.trendKnown;
  d["trendDir"] = s.trendDir;
  d["ageKnown"] = s.ageKnown;
  d["ageMin"] = s.ageMin;
  d["pollEver"] = s.pollEver;
  d["lastOk"] = s.lastOk;
  d["lastErr"] = s.lastErr;
  d["lastPollAgoS"] = (millis() - s.lastPollMs) / 1000;
  sendJson(d);
}

// {"p":[[minutesAgo,mgdl],...]} oldest first, last 12 h. Built by hand: no big JSON document in RAM.
static void handleHistory() {
  if (!authOk()) return;
  String out = "{\"p\":[";
  if (gHist) {
    int32_t now = (int32_t)(millis() / 1000);
    bool first = true;
    for (int i = 0; i < gHist->size(); i++) {
      const HistSample& h = gHist->at(i);
      int32_t age = now - h.t;
      if (age > History::WINDOW_S) continue;
      if (!first) out += ',';
      first = false;
      out += '[';
      out += (age + 30) / 60;
      out += ',';
      out += h.mg;
      out += ']';
    }
  }
  out += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

static void handleTestLogin() {
  if (!authOk()) return;
  JsonDocument in;
  if (!readBody(in)) return sendError("invalid JSON");
  // Test with the submitted values (empty password = use the stored one); nothing is saved.
  static Settings tmp;
  tmp = *gSet;
  settingsApplyJson(in, tmp);
  settingsClamp(tmp);
  LluClient c;
  c.configure(&tmp);
  LluReading r;
  bool ok = c.poll(r);
  JsonDocument out;
  out["ok"] = ok;
  if (ok) out["mgdl"] = r.mgdl;
  else out["error"] = r.err;
  sendJson(out);
}

static void handlePreview() {
  if (!authOk()) return;
  JsonDocument in;
  if (!readBody(in)) return sendError("invalid JSON");
  const char* sn = in["state"] | "keep";
  bool keep = strcmp(sn, "keep") == 0;
  State st = State::Normal;
  if (!keep && !stateFromName(sn, &st)) return sendError("unknown state");
  int c = clampi(in["contrast"] | 5, 1, 255);
  int led = clampi(in["led"] | 5, 1, 255);
  int secs = clampi(in["seconds"] | 20, 1, 120);
  if (gHooks.onPreview) gHooks.onPreview(st, keep, (uint8_t)c, (uint8_t)led, (uint16_t)secs);
  JsonDocument out;
  out["ok"] = true;
  sendJson(out);
}

static void handleReboot() {
  if (!authOk()) return;
  server.send(200, "application/json", "{\"ok\":true}");
  delay(300);
  ESP.restart();
}

static void handleFactoryReset() {
  if (!authOk()) return;
  server.send(200, "application/json", "{\"ok\":true}");
  delay(300);
  settingsFactoryReset();
  ESP.restart();
}

static void handleNotFound() {
  if (viaSetupAp()) {  // captive portal: send any unknown URL to the settings page
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "Not found");
}

void WebUi::begin(Settings* settings, const DeviceStatus* status, const WebUiHooks& hooks) {
  gSet = settings;
  gSt = status;
  gHooks = hooks;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/settings", HTTP_POST, handlePostSettings);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/test-login", HTTP_POST, handleTestLogin);
  server.on("/api/preview", HTTP_POST, handlePreview);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/factory-reset", HTTP_POST, handleFactoryReset);
  server.onNotFound(handleNotFound);
  server.begin();
}

void WebUi::setHistory(const History* h) { gHist = h; }

void WebUi::setSetupMode(bool on) {
  if (on == gAp) return;
  gAp = on;
  if (on) dns.start(53, "*", WiFi.softAPIP());
  else dns.stop();
}

void WebUi::handle() {
  if (gAp) dns.processNextRequest();
  server.handleClient();
}
