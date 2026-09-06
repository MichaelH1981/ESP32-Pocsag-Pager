#pragma once
#include <stdint.h>

// A press is accepted only after the raw input has stayed low for 30 ms.
struct ButtonDebounce {
  bool rawHigh = true;
  bool stableHigh = true;
  uint32_t changedAt = 0;

  bool update(bool high, uint32_t now) {
    if (high != rawHigh) {
      rawHigh = high;
      changedAt = now;
    }
    if (rawHigh != stableHigh && uint32_t(now - changedAt) >= 30) {
      stableHigh = rawHigh;
      return !stableHigh;
    }
    return false;
  }
};
