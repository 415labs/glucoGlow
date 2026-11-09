#pragma once
// Minimal LibreLinkUp (unofficial) client: login, regional redirect, token reuse, latest reading.
#include <Arduino.h>
#include "settings.h"

struct LluReading {
  bool ok = false;
  int mgdl = 0;
  int trendArrow = 0;      // 1..5, 0 = unknown
  bool tsOk = false;
  int64_t tsUtc = 0;       // FactoryTimestamp (UTC) as epoch seconds
  int httpCode = 0;
  char err[80] = {0};
};

struct LluHistPoint {
  int64_t tsUtc;
  uint16_t mg;
};

class LluClient {
 public:
  void configure(const Settings* s);  // pointer must outlive the client
  void invalidate();                  // forget token/host (call when credentials change)
  bool poll(LluReading& out);         // blocking, up to ~20 s worst case
  // The last ~12 h from the server's graph (oldest first). Needs a prior successful poll().
  bool fetchGraph(LluHistPoint* pts, int cap, int& n, LluReading& out);

 private:
  bool login(LluReading& out);
  bool fetchConnections(LluReading& out, int& code);

  const Settings* s_ = nullptr;
  String host_;
  String token_;
  String accountId_;
  String patientId_;
};
