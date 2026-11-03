#pragma once
#include <stdint.h>

// Stay awake across a complete transmission; do not rely on message boundaries.
struct PagerPowerPolicy {
  uint32_t lastActivity = 0;
  uint32_t lastSignal = 0;
  void begin(uint32_t now) { lastActivity = lastSignal = now; }
  void activity(uint32_t now) { lastActivity = now; }
  void signal(uint32_t now) { lastSignal = now; }
  bool maySleep(uint32_t now, bool uiIdle, bool completeBatchPending) const {
    return uiIdle && !completeBatchPending &&
           uint32_t(now - lastActivity) >= 8000 &&
           uint32_t(now - lastSignal) >= 3000;
  }
};
