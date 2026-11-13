#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_system.h>
#include <time.h>

#include "display.h"
#include "glucose.h"
#include "history.h"
#include "llu_client.h"
#include "settings.h"
#include "settings_store.h"
#include "status.h"
#include "webui.h"

static const int PIN_LED = 8;   // onboard LED (active low): kept OFF so it never lights the room
static const int PIN_BOOT = 9;  // BOOT button: tap = info screens, hold 3 s = setup mode on/off

static const uint32_t SHORT_PRESS_MAX_MS = 500;
static const uint32_t HOLD_MS = 3000;
static const uint32_t PAGE_MS = 3500;                       // each setup instruction screen
static const uint32_t SETUP_IDLE_MS = 15UL * 60UL * 1000UL;  // setup WiFi turns off after 15 min w/o clients
static const uint32_t PROVISION_RETRY_MS = 120UL * 1000UL;   // no WiFi + nobody connected: reboot and retry

static Settings gSet;
static DeviceStatus gSt;
static Display gDisp;
static History gHist;
static LluClient gLlu;
static WebUi gWeb;

// Latest good reading
static bool gHave = false;
static int gMgdl = 0;
static int gArrow = 0;
static bool gTsOk = false;
static int64_t gTsUtc = 0;
static uint32_t gRecvMs = 0;

// Self-healing: if no reading arrives for a long time because of a network problem, recover by
// reconnecting WiFi, forgetting the login, and as a last resort restarting (the cause can be a
// WiFi link that never came back or a fragmented heap that can no longer open a TLS connection).
static const uint32_t WD_MAGIC = 0x474C4F57;
static RTC_NOINIT_ATTR uint32_t gWdMagic;
static RTC_NOINIT_ATTR uint32_t gWdCount;  // consecutive watchdog restarts (survives a restart)
static uint32_t gLastOkMs = 0;             // last successful poll (boot counts as the start)
static bool gNetFail = false;              // last poll failed at network/TLS level, not on credentials
static uint32_t gWifiDownSince = 0, gLastWifiKick = 0;

static uint32_t gNextPoll = 0;
static bool gPollNow = false;
static uint32_t gFailCount = 0;

// Setup WiFi (access point) state
static bool gSetupAp = false;
static bool gProvisioning = false;  // true = no working WiFi, setup WiFi can't be switched off
static uint32_t gApStartMs = 0;
static uint32_t gApBusyMs = 0;      // last time a client was connected to the setup WiFi
static uint8_t gLastSlot = 255;
static uint32_t gBusyUntil = 0;     // button-driven screens own the display until then
static bool busyAt(uint32_t now) { return (int32_t)(gBusyUntil - now) > 0; }  // wrap-safe

static bool timeValid() { return time(nullptr) > 1700000000; }

static void applyTime() { configTzTime(gSet.tz, "pool.ntp.org", "time.google.com"); }

static void onSettingsSaved() {
  applyTime();
  gLlu.configure(&gSet);     // new credentials / region / TLS mode / app version
  gDisp.applySettings(gSet);  // screen rotation
  gFailCount = 0;
  gPollNow = true;           // poll right away with the new settings
}

static void onPreview(State st, bool keep, uint8_t contrast, uint16_t secs) {
  gDisp.preview(st, keep, contrast, secs);
}

// This ESP32-C3 0.42" OLED board's setup network was invisible at the default (maximum) WiFi
// transmit power, and worked at 8.5 dBm (tested by the owner). Override with -DWIFI_TX_POWER=...
#ifndef WIFI_TX_POWER
#define WIFI_TX_POWER WIFI_POWER_8_5dBm
#endif
static void applyTxPower() { WiFi.setTxPower(WIFI_TX_POWER); }

static bool startStation() {
  if (!gSet.wifiSsid[0]) return false;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(gSet.hostname);
  WiFi.setAutoReconnect(true);
  WiFi.begin(gSet.wifiSsid, gSet.wifiPass);
  applyTxPower();
  gDisp.showNow("Joining", "WiFi:", gSet.wifiSsid);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(200);
  return WiFi.status() == WL_CONNECTED;
}

// ---------------------------------------------------------------------------------------------
// Setup WiFi (access point). provisioning = true when there is no working WiFi connection.
// ---------------------------------------------------------------------------------------------
static void startSetupAp(bool provisioning) {
  gProvisioning = provisioning;
  gSt.provisioning = provisioning;
  WiFi.persistent(false);
  if (provisioning) {
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP);
  } else {
    WiFi.mode(WIFI_AP_STA);  // keep the normal WiFi connection (and glucose monitoring) running
  }
  delay(100);
  // Channel is ignored while the station connection is up (one radio, one channel).
  bool ok = WiFi.softAP(AP_SSID, gSet.apPass, 6);
  if (!ok) {
    delay(300);
    ok = WiFi.softAP(AP_SSID, gSet.apPass);
  }
  applyTxPower();
  gSetupAp = ok;
  gWeb.setSetupMode(ok);
  gApStartMs = gApBusyMs = millis();
  gLastSlot = 255;
  Serial.printf("[setup] access point %s, SSID %s, IP %s\n", ok ? "started" : "FAILED", AP_SSID,
                WiFi.softAPIP().toString().c_str());
  if (!ok) gDisp.overlay("SETUP", "FAILED", "Replug me", "", 6000);
}

static void stopSetupAp() {
  if (!gSetupAp || gProvisioning) return;  // without it there would be no way to reach the device
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  applyTxPower();
  gSetupAp = false;
  gWeb.setSetupMode(false);
  gDisp.clearOverlay();
  Serial.println("[setup] access point stopped");
}

// Setup instruction screens, drawn large. Three steps, each one screen or two:
//   1  join the setup WiFi (name, then password)   2  open the address in a browser
//   3  fill in WiFi + LibreLink and save
static const uint8_t SETUP_PAGES = 6;

static void showSetupPage(int page, uint32_t ms) {
  switch (page) {
    case 0: gDisp.overlay("1/3 JOIN", AP_SSID, "", "", ms); break;       // the WiFi name
    case 1: gDisp.overlay("1/3 PASS", gSet.apPass, "", "", ms); break;   // its password
    case 2: gDisp.overlay("2/3 OPEN", "browser", "", "", ms); break;
    case 3: gDisp.overlay("2/3 URL", "192.168.4.1", "", "", ms); break;  // type this address
    case 4: gDisp.overlay("3/3 ENTER", "WiFi &", "LibreLink", "", ms); break;
    default:
      if (gProvisioning) gDisp.overlay("3/3 THEN", "tap Save", "& Reboot", "", ms);
      else gDisp.overlay("3/3 THEN", "tap Save", "", "", ms);
  }
}

// Called every render tick while the setup WiFi is on: steps through the instruction screens. Not
// provisioning: the live glucose view gets one slot at the end of each round. The instructions
// are never skipped, even when glucose is critical (the glucose slot still shows that alert).
static void updateSetupScreens(const View&) {
  if (!gSetupAp) return;
  if (busyAt(millis())) return;  // a button-driven screen is showing
  const uint32_t slots = SETUP_PAGES + (gProvisioning ? 0 : 1);
  uint8_t slot = (uint8_t)(((millis() - gApStartMs) / PAGE_MS) % slots);
  if (slot == gLastSlot) return;
  gLastSlot = slot;
  if (slot >= SETUP_PAGES) gDisp.clearOverlay();
  else showSetupPage(slot, PAGE_MS + 300);
}

// ---------------------------------------------------------------------------------------------
// BOOT button: tap = info screens, hold 3 s = setup mode on/off (countdown shown on the display)
// ---------------------------------------------------------------------------------------------
static void showInfoPage(int page, uint32_t ms) {
  if (page == 1) {
    String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("no WiFi");
    gDisp.overlay("WEB APP", ip.c_str(), "", "", ms);
  } else if (page == 2) {
    char host[32];
    snprintf(host, sizeof host, "%s.local", gSet.hostname);
    gDisp.overlay("OR TYPE", host, "", "", ms);
  } else {
    gDisp.overlay("SETUP?", "Hold BOOT", "for 3 s", "", ms);
  }
}

static void toggleSetupMode() {
  if (gSetupAp) {
    if (gProvisioning) {
      gDisp.overlay("SETUP", "Save WiFi", "& Reboot", "", 3000);
      gBusyUntil = millis() + 3000;
    } else {
      stopSetupAp();
      gDisp.overlay("SETUP", "MODE OFF", "", "", 2000);
      gBusyUntil = millis() + 2000;
    }
  } else {
    gDisp.showNow("SETUP", "starting");
    startSetupAp(false);
    gDisp.overlay("SETUP ON", "Follow", "steps", "", 1500);
    gBusyUntil = millis() + 1500;
  }
}

// The poll (HTTPS login + download) blocks the main loop for seconds. The button is therefore also
// watched by an interrupt that records when it went down and up, so a 3 s hold is recognised
// even when the loop was busy, and polls are not started while the button is held.
static volatile uint32_t gIsrDownMs = 0, gIsrUpMs = 0;
static volatile uint32_t gIsrSeq = 0;  // counts presses
static volatile uint32_t gIsrLastEdge = 0;
static bool gBtnDown = false;

static void IRAM_ATTR onButtonEdge() {
  uint32_t n = millis();
  if (n - gIsrLastEdge < 25) return;  // contact bounce
  gIsrLastEdge = n;
  if (digitalRead(PIN_BOOT) == LOW) {
    gIsrDownMs = n;
    gIsrSeq = gIsrSeq + 1;
  } else {
    gIsrUpMs = n;
  }
}

static void handleButton() {
  static bool down = false;
  static bool toggled = false;
  static uint32_t t0 = 0;
  static uint32_t handledSeq = 0;
  static uint8_t infoPage = 0;
  uint32_t now = millis();
  bool pressed = digitalRead(PIN_BOOT) == LOW;
  if (!down && !busyAt(now)) infoPage = 0;  // info screens timed out: next tap starts over

  uint32_t seq = gIsrSeq, isrDown = gIsrDownMs, isrUp = gIsrUpMs;
  if (!down && !pressed && seq != handledSeq) {
    // A whole press happened while the loop was busy (e.g. during a poll).
    handledSeq = seq;
    if (isrUp - isrDown < 0x80000000UL && isrUp - isrDown >= HOLD_MS && now - isrUp < 10000) {
      toggleSetupMode();
      gBtnDown = false;
      return;
    }
  }
  if (pressed && !down) {
    down = true;
    toggled = false;
    handledSeq = seq;
    // Count from the real press, even if the loop only noticed it later.
    t0 = (seq != 0 && now - isrDown < 60000UL) ? isrDown : now;
  }
  gBtnDown = down;
  if (down && pressed && !toggled && now - t0 >= SHORT_PRESS_MAX_MS) {
    uint32_t held = now - t0;
    if (held >= HOLD_MS) {
      toggled = true;
      toggleSetupMode();
    } else {
      char l3[16];
      snprintf(l3, sizeof l3, "in %d s", (int)((HOLD_MS - held + 999) / 1000));
      gDisp.overlay("KEEP HOLD", gSetupAp ? "to EXIT" : "to ENTER", l3, "", 400);
      gBusyUntil = now + 400;
    }
  }
  if (down && !pressed) {
    uint32_t held = (isrUp - t0 < 0x80000000UL && isrUp >= t0) ? isrUp - t0 : now - t0;
    down = false;
    gBtnDown = false;
    if (!toggled) {
      if (held >= HOLD_MS) {  // the hold completed while the loop was busy
        toggleSetupMode();
      } else if (held >= 30 && held < SHORT_PRESS_MAX_MS) {  // a tap (ignores contact bounce)
        infoPage = (uint8_t)((infoPage + 1) % 4);
        if (infoPage == 0) {
          gDisp.clearOverlay();
          gBusyUntil = 0;
        } else {
          showInfoPage(infoPage, 8000);
          gBusyUntil = now + 8000;
        }
      } else {  // released during the countdown: cancel
        gDisp.clearOverlay();
        gBusyUntil = 0;
      }
    }
  }
}

// ---------------------------------------------------------------------------------------------
// Polling
// ---------------------------------------------------------------------------------------------
// First successful poll with an empty chart: pull the last ~12 h from the server (max 3 tries).
static bool gAfterCrash = false;  // last reset was a crash/watchdog: skip the optional server backfill

static void backfillHistory() {
  static uint8_t tries = 0;
#ifndef MOCK_GLUCOSE
#ifdef NO_BACKFILL
  return;  // diagnostic build: never ask the server for old data
#endif
  if (gAfterCrash || gHist.size() > 0 || tries >= 3 || !timeValid()) return;
  tries++;
  static LluHistPoint pts[200];
  int n = 0;
  LluReading r;
  if (!gLlu.fetchGraph(pts, 200, n, r)) {
    Serial.printf("[hist] backfill failed: %s\n", r.err);
    return;
  }
  int32_t nowS = (int32_t)(millis() / 1000);
  int64_t nowE = time(nullptr);
  for (int i = 0; i < n; i++) {
    int64_t ago = nowE - pts[i].tsUtc;
    if (ago < 0 || ago > History::WINDOW_S) continue;
    gHist.add(nowS - (int32_t)ago, pts[i].mg);
  }
  Serial.printf("[hist] backfilled %d points from the server\n", gHist.size());
#endif
}

static void doPoll() {
  gSt.pollEver = true;
  gSt.lastPollMs = millis();
  LluReading r;
  bool ok;
#ifdef MOCK_GLUCOSE
  static const int vals[] = {110, 95, 65, 48, 180, 250, -1};
  static uint8_t idx = 0;
  int v = vals[idx++ % 7];
  ok = v > 0;
  r.ok = ok;
  r.mgdl = v;
  r.trendArrow = (idx % 5) + 1;
  r.tsOk = timeValid();
  r.tsUtc = time(nullptr);
  if (!ok) snprintf(r.err, sizeof r.err, "mock: simulated outage");
#else
  ok = gLlu.poll(r);
#endif
  if (ok) {
    gHave = true;
    gMgdl = r.mgdl;
    gArrow = r.trendArrow;
    gTsOk = r.tsOk;
    gTsUtc = r.tsUtc;
    gRecvMs = millis();
    backfillHistory();  // before adding the live reading, so older points go in first
    gHist.add((int32_t)(millis() / 1000), (uint16_t)r.mgdl);
    gFailCount = 0;
    gLastOkMs = millis();
    gNetFail = false;
    gWdCount = 0;
    gSt.lastOk = true;
    gSt.lastErr[0] = 0;
    Serial.printf("[poll] %d mg/dL, trend %d\n", r.mgdl, r.trendArrow);
  } else {
    gFailCount++;
    gNetFail = r.httpCode <= 0;
    if (gFailCount == 2) gLlu.invalidate();  // maybe the login went bad: start from a fresh one
    Serial.printf("[poll] free heap %u, largest block %u\n", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());
    gSt.lastOk = false;
    strncpy(gSt.lastErr, r.err, sizeof gSt.lastErr - 1);
    Serial.printf("[poll] failed: %s (HTTP %d)\n", r.err, r.httpCode);
  }
  gSt.failCount = gFailCount;

  // Next poll: the configured interval; after failures back off (x2 each time, max 10 min).
  uint32_t interval = (uint32_t)gSet.pollSec * 1000UL;
  if (gFailCount) {
    uint32_t backoff = 30000UL << (gFailCount > 5 ? 5 : gFailCount - 1);
    if (backoff > 600000UL) backoff = 600000UL;
    if (backoff > interval) interval = backoff;
  }
  gNextPoll = millis() + interval;
}

static View buildView() {
  View v;
  bool tv = timeValid();
  if (gHave) {
    int ageMin;
    if (gTsOk && tv) ageMin = ageMinutes(time(nullptr), gTsUtc);
    else ageMin = (int)((millis() - gRecvMs) / 60000UL);  // fallback before NTP sync
    // Also stale when polling keeps failing for a long time even if the sensor timestamp looks fresh.
    uint32_t sinceRecvMin = (millis() - gRecvMs) / 60000UL;
    bool stale = ageMin > (int)gSet.staleMin || sinceRecvMin > gSet.staleMin;
    v.haveValue = true;
    v.mgdl = gMgdl;
    v.state = classify(gMgdl, stale, gSet);
    v.ageKnown = true;
    v.ageMin = ageMin;
    int dir;
    v.trendKnown = trendDir(gArrow, &dir);
    v.trendDir = v.trendKnown ? dir : 0;
  } else {
    // Nothing yet: neutral "--" until a few polls have failed, then flag it.
    v.state = (gFailCount >= 3 || millis() > (uint32_t)gSet.staleMin * 60000UL) ? State::Stale
                                                                                : State::Unknown;
  }
  return v;
}

void setup() {
  Serial.begin(115200);
  // Native USB: give the serial monitor up to 3 s to attach so boot messages aren't lost.
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 3000;) delay(10);
  Serial.printf("\n[boot] glucoGlow v%s\n", FW_VERSION);
  esp_reset_reason_t why = esp_reset_reason();
  gAfterCrash = why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT ||
                why == ESP_RST_WDT || why == ESP_RST_BROWNOUT;
  Serial.printf("[boot] reset reason %d%s\n", (int)why, gAfterCrash ? " (CRASH)" : "");
  if (gWdMagic != WD_MAGIC || why == ESP_RST_POWERON) {
    gWdMagic = WD_MAGIC;
    gWdCount = 0;
  }
  gLastOkMs = millis();
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);  // off
  pinMode(PIN_BOOT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BOOT), onButtonEdge, CHANGE);

  settingsLoad(gSet);
  if (settingsEnsureApPass(gSet)) settingsSave(gSet);  // first boot: generate the setup WiFi password

  gDisp.begin();
  gDisp.applySettings(gSet);
  gDisp.setHistory(&gHist);
  gDisp.intro("v" FW_VERSION);  // animated welcome
  if (gAfterCrash) {
    char code[16];
    snprintf(code, sizeof code, "code %d", (int)why);
    gDisp.showNow("CRASHED", "restarted", code);
    delay(2500);
  }
  gDisp.showNow("SETUP?", "Hold BOOT", "for 3 s");  // always shown at boot
  delay(2500);

  gLlu.configure(&gSet);

  if (startStation()) {
    applyTime();
    if (MDNS.begin(gSet.hostname)) MDNS.addService("http", "tcp", 80);
    Serial.printf("[net] http://%s.local  (%s)\n", gSet.hostname, WiFi.localIP().toString().c_str());
    gNextPoll = millis() + 2000;  // first poll shortly after boot (lets NTP start)
  } else {
    startSetupAp(true);  // no (working) WiFi: the setup WiFi is the only way in
  }

  // The web server must start after WiFi is initialised (station or access point).
  WebUiHooks hooks;
  hooks.onSettingsSaved = onSettingsSaved;
  hooks.onPreview = onPreview;
  gWeb.begin(&gSet, &gSt, hooks);
  gWeb.setHistory(&gHist);
}

// Runs every loop. Never acts while someone is using the setup WiFi or holding BOOT.
static void selfHeal(uint32_t now) {
  if (gProvisioning || gBtnDown || !gSet.wifiSsid[0]) return;
  bool wifiUp = WiFi.status() == WL_CONNECTED;
  if (wifiUp) {
    gWifiDownSince = 0;
  } else {
    if (!gWifiDownSince) gWifiDownSince = now;
    if (now - gWifiDownSince > 30000UL && now - gLastWifiKick > 30000UL) {
      gLastWifiKick = now;  // the automatic reconnect did not work: join again by hand
      Serial.println("[wifi] still down, reconnecting");
      WiFi.disconnect(false);
      WiFi.begin(gSet.wifiSsid, gSet.wifiPass);
    }
  }
  if (!gSet.lluEmail[0]) return;
  uint32_t minutes = 20UL + 20UL * (gWdCount > 5 ? 5 : gWdCount);  // 20, 40 ... 120 min
  bool setupInUse = gSetupAp && WiFi.softAPgetStationNum() > 0;
  if (!setupInUse && (!wifiUp || gNetFail) && now - gLastOkMs > minutes * 60000UL) {
    Serial.printf("[watchdog] no reading for %u min (network problem): restarting\n",
                  (unsigned)minutes);
    gWdCount = gWdCount + 1;
    delay(200);
    ESP.restart();
  }
}

void loop() {
  static uint32_t lastRender = 0;
  static uint32_t lastHint = 0;

  gWeb.handle();
  handleButton();

  uint32_t now = millis();
  selfHeal(now);
  bool staUp = !gProvisioning && WiFi.status() == WL_CONNECTED;
  if (staUp && !gBtnDown && (gPollNow || (int32_t)(now - gNextPoll) >= 0)) {
    gPollNow = false;
    doPoll();  // blocking up to ~20 s in the worst case; the web app waits meanwhile
    now = millis();
  }

  // Setup WiFi housekeeping
  if (gSetupAp) {
    if (WiFi.softAPgetStationNum() > 0) gApBusyMs = now;
    if (!gProvisioning && now - gApBusyMs > SETUP_IDLE_MS) {  // forgotten setup WiFi: switch it off
      stopSetupAp();
      gDisp.overlay("SETUP", "ENDED", "(idle)", "", 3000);
      gBusyUntil = now + 3000;
    } else if (gProvisioning && gSet.wifiSsid[0] && now - gApBusyMs > PROVISION_RETRY_MS) {
      ESP.restart();  // router was probably still booting: try the saved WiFi again
    }
  }

  if (now - lastRender >= 50) {
    lastRender = now;
    View v = buildView();
    bool tv = timeValid();
    int minOfDay = 0;
    if (tv) {
      struct tm t;
      time_t n = time(nullptr);
      localtime_r(&n, &t);
      minOfDay = t.tm_hour * 60 + t.tm_min;
    }

    updateSetupScreens(v);

#ifndef MOCK_GLUCOSE
    // Nothing configured yet: remind how to get to the settings every 20 s
    if (!gSetupAp && !gSet.lluEmail[0] && !busyAt(now) && now - lastHint > 20000) {
      lastHint = now;
      gDisp.overlay("SET ME UP", "Hold BOOT", "for 3 s", "", 5000);
    }
#endif

    gDisp.update(v, gSet, tv, minOfDay);
    gSt.timeValid = tv;
    gSt.night = gDisp.isNight();
    gSt.minuteOfDay = tv ? minOfDay : -1;
    gSt.state = v.state;
    gSt.haveValue = v.haveValue;
    gSt.mgdl = v.mgdl;
    gSt.trendKnown = v.trendKnown;
    gSt.trendDir = v.trendDir;
    gSt.ageKnown = v.ageKnown;
    gSt.ageMin = v.ageMin;
  }

  delay(5);
}
