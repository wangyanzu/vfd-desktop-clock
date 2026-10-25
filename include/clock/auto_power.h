#pragma once
#include "clock/core.h"

namespace clockapp {
// One awake session, independent of network traffic or displayed data updates.
class AutoPower {
 public:
  static constexpr uint32_t dimAfterMs = 60000;
  static constexpr uint32_t sleepAfterMs = 30 * 60 * 1000;
  void restart(uint32_t now) { awakeAt_ = now; }
  bool dimmed(uint32_t now) const { return elapsed(now, awakeAt_, dimAfterMs); }
  bool sleepDue(uint32_t now) const { return elapsed(now, awakeAt_, sleepAfterMs); }
  uint8_t brightness(uint32_t now, uint8_t normal) const {
    // Legacy settings may still contain level 4. The dim phase must be lower.
    return !dimmed(now) ? 4 : normal < 1 ? 1 : normal > 3 ? 3 : normal;
  }
  uint32_t awakeSeconds(uint32_t now) const { return uint32_t(now - awakeAt_) / 1000; }
 private:
  uint32_t awakeAt_ = 0;
};
}
