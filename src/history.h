#pragma once
// Rolling glucose history for the chart: one sample per 5 minutes, up to 12.5 h, kept in RAM
// (cleared by a reboot). Times are seconds of uptime, so the chart works before NTP has synced.
#include <stdint.h>

struct HistSample {
  int32_t t;    // uptime seconds when recorded (negative = before this boot, from backfill)
  uint16_t mg;  // mg/dL
};

class History {
 public:
  static const int CAP = 150;               // 150 x 5 min = 12.5 h
  static const int32_t STEP_S = 300;        // minimum spacing between stored samples
  static const int32_t WINDOW_S = 12 * 3600;

  // Samples must arrive oldest first; ones closer than STEP_S to the previous sample are skipped.
  void add(int32_t nowS, uint16_t mg) {
    if (n_ > 0 && nowS - at(n_ - 1).t < STEP_S) return;
    int idx = (head_ + n_) % CAP;
    if (n_ == CAP) { head_ = (head_ + 1) % CAP; idx = (head_ + n_ - 1) % CAP; }
    else n_++;
    buf_[idx] = {nowS, mg};
  }
  int size() const { return n_; }
  const HistSample& at(int i) const { return buf_[(head_ + i) % CAP]; }  // 0 = oldest
  // Number of samples no older than 12 h at `nowS`.
  int sizeInWindow(int32_t nowS) const {
    int c = 0;
    for (int i = 0; i < n_; i++) if (nowS - at(i).t <= WINDOW_S) c++;
    return c;
  }
  void clear() { n_ = 0; head_ = 0; }

 private:
  HistSample buf_[CAP];
  int n_ = 0, head_ = 0;
};
