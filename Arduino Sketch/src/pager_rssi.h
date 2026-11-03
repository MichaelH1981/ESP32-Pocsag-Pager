#pragma once
#include <stdint.h>

// Cache register readings taken by the loop. A short peak window avoids showing
// the noise floor when the carrier ends just before the final word is decoded.
// This is an indication for the last checked reception, not a calibrated meter.
class PagerRssi {
 public:
  void sample(float dbm, uint32_t now) {
    if (!(dbm >= -127.5f && dbm <= 0.0f)) return;
    samples_[next_] = {dbm, now};
    next_ = (next_ + 1) % Capacity;
    if (count_ < Capacity) ++count_;
  }
  void received(uint32_t now) {
    bool found = false;
    float peak = -150;
    for (unsigned i = 0; i < count_; ++i) {
      if (uint32_t(now - samples_[i].at) <= 500) {
        if (samples_[i].dbm > peak) peak = samples_[i].dbm;
        found = true;
      }
    }
    if (found) { last_ = peak; valid_ = true; }
  }
  bool valid() const { return valid_; }
  float last() const { return last_; }
 private:
  enum { Capacity = 16 };
  struct Sample { float dbm; uint32_t at; };
  Sample samples_[Capacity]{};
  unsigned next_ = 0, count_ = 0;
  float last_ = 0;
  bool valid_ = false;
};
