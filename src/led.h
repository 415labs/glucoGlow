#pragma once
// Optional external WS2812 ("NeoPixel") RGB LED: the OLED is monochrome white, so red / orange /
// green status colors need a separate LED. Wiring: DIN -> the GPIO set in the web app, VCC -> 3V3,
// GND -> GND. Disabled (pin -1) by default.
#include <Arduino.h>

class Led {
 public:
  void begin(int8_t pin);                  // (re)assign the pin; -1 disables and turns the LED off
  void set(uint8_t r, uint8_t g, uint8_t b);
  bool enabled() const { return pin_ >= 0; }

 private:
  int8_t pin_ = -1;
  bool init_ = false;
  bool haveLast_ = false;
  uint8_t r_ = 0, g_ = 0, b_ = 0;
};
