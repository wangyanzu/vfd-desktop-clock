#pragma once
#include "clock/core.h"
namespace clockapp {
class BurnInShift {
 public:
  int8_t offset(uint32_t now) {
    const uint32_t steps=uint32_t(now-steppedAt_)/300000;
    if (steps) { phase_=uint8_t((phase_+steps)%8); steppedAt_+=steps*300000; }
    static constexpr int8_t positions[]={0,1,2,1,0,-1,-2,-1};
    return positions[phase_];
  }
 private:
  uint32_t steppedAt_=0;
  uint8_t phase_=0;
};
}
