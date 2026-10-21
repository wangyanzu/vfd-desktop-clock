#pragma once
#include "clock/core.h"
namespace clockapp {
class RetrySchedule {
 public:
  bool ready(uint32_t now) const { return !waiting_ || elapsed(now, failedAt_, delay_); }
  void failed(uint32_t now) {
    if (waiting_) delay_ = delay_ < 30000 ? delay_ * 2 : 60000;
    else delay_ = 1000;
    failedAt_ = now; waiting_ = true;
  }
  void reset() { waiting_ = false; delay_ = 1000; }
 private:
  uint32_t failedAt_ = 0, delay_ = 1000;
  bool waiting_ = false;
};
}
