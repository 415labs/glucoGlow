#include "llu_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>

#include "glucose.h"

// Default certificate bundle embedded by Arduino-ESP32 2.0.x
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

static void setErr(LluReading& r, const char* msg) {
  strncpy(r.err, msg, sizeof(r.err) - 1);
  r.err[sizeof(r.err) - 1] = 0;
}

static String sha256Hex(const String& in) {
  uint8_t out[32];
  mbedtls_sha256((const unsigned char*)in.c_str(), in.length(), out, 0);
  static const char* hex = "0123456789abcdef";
  String s;
  for (int i = 0; i < 32; i++) {
    s += hex[out[i] >> 4];
    s += hex[out[i] & 15];
  }
  return s;
}

// One HTTPS request. Returns HTTP status (or negative HTTPClient error). Body goes to `out`.
static int httpJson(bool post, const String& url, const String& body, const String& token,
                    const String& accountId, bool insecure, const char* appVersion, String& out) {
  WiFiClientSecure client;
  if (insecure) client.setInsecure();
  else client.setCACertBundle(rootca_crt_bundle_start);
  HTTPClient http;
  http.setTimeout(10000);
  http.setReuse(false);
  if (!http.begin(client, url)) return -100;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("cache-control", "no-cache");
  http.addHeader("product", "llu.android");
  http.addHeader("version", appVersion);  // the unofficial API rejects versions that are too old
  if (token.length()) {
    http.addHeader("Authorization", "Bearer " + token);
    if (accountId.length()) http.addHeader("account-id", accountId);
  }
  int code = post ? http.POST(body) : http.GET();
  if (code > 0) out = http.getString();
  http.end();
  return code;
}

void LluClient::configure(const Settings* s) {
  s_ = s;
  invalidate();
}

void LluClient::invalidate() {
  token_ = "";
  accountId_ = "";
  host_ = (s_ && s_->lluRegion[0]) ? String("api-") + s_->lluRegion + ".libreview.io"
                                   : String("api.libreview.io");
}

bool LluClient::login(LluReading& out) {
  if (!s_ || !s_->lluEmail[0] || !s_->lluPass[0]) {
    setErr(out, "LibreLinkUp email/password not set");
    return false;
  }
  for (int attempt = 0; attempt < 3; attempt++) {
    JsonDocument req;
    req["email"] = s_->lluEmail;
    req["password"] = s_->lluPass;
    String body, resp;
    serializeJson(req, body);
    int code = httpJson(true, "https://" + host_ + "/llu/auth/login", body, "", "",
                        s_->tlsInsecure, s_->lluVersion, resp);
    out.httpCode = code;
    if (code <= 0) {
      setErr(out, "login: network/TLS error (try 'skip certificate check')");
      return false;
    }
    if (code == 429) { setErr(out, "login: rate limited (HTTP 429)"); return false; }

    JsonDocument filter;
    filter["status"] = true;
    filter["data"]["redirect"] = true;
    filter["data"]["region"] = true;
    filter["data"]["user"]["id"] = true;
    filter["data"]["authTicket"]["token"] = true;
    JsonDocument d;
    if (deserializeJson(d, resp, DeserializationOption::Filter(filter))) {
      setErr(out, "login: unreadable response");
      return false;
    }
    if (d["data"]["redirect"].as<bool>()) {  // account lives in another region
      const char* region = d["data"]["region"] | "";
      if (!*region) { setErr(out, "login: redirect without region"); return false; }
      host_ = String("api-") + region + ".libreview.io";
      continue;
    }
    const char* tok = d["data"]["authTicket"]["token"] | "";
    const char* uid = d["data"]["user"]["id"] | "";
    if (!*tok) {
      char m[80];
      snprintf(m, sizeof m, "login failed (HTTP %d, status %d)", code, d["status"] | -1);
      setErr(out, m);
      return false;
    }
    token_ = tok;
    accountId_ = *uid ? sha256Hex(uid) : String("");
    return true;
  }
  setErr(out, "login: too many redirects");
  return false;
}

bool LluClient::fetchConnections(LluReading& out, int& code) {
  String resp;
  code = httpJson(false, "https://" + host_ + "/llu/connections", "", token_, accountId_,
                  s_->tlsInsecure, s_->lluVersion, resp);
  out.httpCode = code;
  if (code != 200) return false;

  JsonDocument filter;
  filter["data"][0]["patientId"] = true;
  filter["data"][0]["glucoseMeasurement"]["Value"] = true;
  filter["data"][0]["glucoseMeasurement"]["ValueInMgPerDl"] = true;
  filter["data"][0]["glucoseMeasurement"]["TrendArrow"] = true;
  filter["data"][0]["glucoseMeasurement"]["FactoryTimestamp"] = true;
  JsonDocument d;
  if (deserializeJson(d, resp, DeserializationOption::Filter(filter))) {
    setErr(out, "connections: unreadable response");
    return false;
  }
  patientId_ = d["data"][0]["patientId"] | "";
  JsonVariantConst gm = d["data"][0]["glucoseMeasurement"];
  if (gm.isNull()) {
    setErr(out, "no glucose data (is a follower connection shared?)");
    return false;
  }
  // Prefer the explicit mg/dL field; otherwise assume Value is mg/dL.
  float v = gm["ValueInMgPerDl"] | (gm["Value"] | 0.0f);
  if (v <= 0) { setErr(out, "reading has no value"); return false; }
  out.mgdl = (int)(v + 0.5f);
  out.trendArrow = gm["TrendArrow"] | 0;
  const char* ts = gm["FactoryTimestamp"] | "";
  out.tsOk = parseLluTimestamp(ts, &out.tsUtc);
  out.ok = true;
  return true;
}

bool LluClient::fetchGraph(LluHistPoint* pts, int cap, int& n, LluReading& out) {
  n = 0;
  if (!s_ || token_.isEmpty() || patientId_.isEmpty()) { setErr(out, "graph: not logged in"); return false; }
  String resp;
  int code = httpJson(false, "https://" + host_ + "/llu/connections/" + patientId_ + "/graph", "",
                      token_, accountId_, s_->tlsInsecure, s_->lluVersion, resp);
  out.httpCode = code;
  if (code != 200) { char m[80]; snprintf(m, sizeof m, "graph failed (HTTP %d)", code); setErr(out, m); return false; }
  JsonDocument filter;  // keep only the three fields we need from each point
  filter["data"]["graphData"][0]["Value"] = true;
  filter["data"]["graphData"][0]["ValueInMgPerDl"] = true;
  filter["data"]["graphData"][0]["FactoryTimestamp"] = true;
  JsonDocument d;
  if (deserializeJson(d, resp, DeserializationOption::Filter(filter))) {
    setErr(out, "graph: unreadable response");
    return false;
  }
  resp = "";
  for (JsonVariantConst g : d["data"]["graphData"].as<JsonArrayConst>()) {
    if (n >= cap) break;
    float v = g["ValueInMgPerDl"] | (g["Value"] | 0.0f);
    int64_t ts;
    if (v <= 0 || !parseLluTimestamp(g["FactoryTimestamp"] | "", &ts)) continue;
    pts[n++] = {ts, (uint16_t)(v + 0.5f)};
  }
  return n > 0;
}

bool LluClient::poll(LluReading& out) {
  out = LluReading();
  if (!s_) { setErr(out, "not configured"); return false; }
  if (token_.isEmpty() && !login(out)) return false;

  int code = 0;
  if (fetchConnections(out, code)) return true;
  if (code == 401 || code == 403) {  // token expired: log in once more and retry
    token_ = "";
    if (!login(out)) return false;
    if (fetchConnections(out, code)) return true;
  }
  if (!out.err[0]) {
    char m[80];
    if (code == 429) snprintf(m, sizeof m, "rate limited (HTTP 429)");
    else if (code <= 0) snprintf(m, sizeof m, "network/TLS error (%d)", code);
    else snprintf(m, sizeof m, "connections failed (HTTP %d)", code);
    setErr(out, m);
  }
  return false;
}
