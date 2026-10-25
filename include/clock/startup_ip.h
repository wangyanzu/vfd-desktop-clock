#pragma once
#include "clock/core.h"

namespace clockapp {
// Show the first acquired station IP for four seconds of actual settled display.
// Reading time pauses during manual sleep; ordinary reconnects do not repeat it.
class StartupIp {
 public:
  static constexpr uint32_t pageKey=12;
  bool update(uint32_t now,bool connected,bool displayAwake,bool pageVisible,const char* address) {
    if (done_) return false;
    if (!connected) { timing_=false; return false; }
    if (!address || !*address || std::strcmp(address,"0.0.0.0")==0) return false;
    if (std::strncmp(address_,address,sizeof(address_))!=0) {
      std::snprintf(address_,sizeof(address_),"%s",address); timing_=false;
    }
    if (!displayAwake) { timing_=false; return false; }
    if (!timing_ && pageVisible) { shownAt_=now; timing_=true; }
    if (timing_ && elapsed(now,shownAt_,4000)) { done_=true; return false; }
    return true;
  }
  CellFrame frame() const {
    CellFrame content; content.setAscii(0,"WiFi connected"); content.setAscii(1,address_); return content;
  }
 private:
  char address_[16]{};
  uint32_t shownAt_=0;
  bool timing_=false,done_=false;
};
}
