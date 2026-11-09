#include "led.h"

void Led::begin(int8_t pin) {
  if (init_ && pin == pin_) return;
  if (init_ && pin_ >= 0) neopixelWrite(pin_, 0, 0, 0);  // switch the old LED off
  pin_ = pin;
  init_ = true;
  haveLast_ = false;
  if (pin_ >= 0) set(0, 0, 0);
}

void Led::set(uint8_t r, uint8_t g, uint8_t b) {
  if (pin_ < 0) return;
  if (haveLast_ && r == r_ && g == g_ && b == b_) return;  // avoid needless RMT traffic
  neopixelWrite(pin_, r, g, b);
  r_ = r;
  g_ = g;
  b_ = b;
  haveLast_ = true;
}
