#pragma once
#include "clock/core.h"

namespace clockapp {
// Pure policy; the adapter owns Wi-Fi APIs. Automatic setup opens once per boot
// (or explicit request) and closes after 10 minutes / 60 seconds connected.
class NetworkPolicy {
 public:
  struct Actions { bool retry = false, openSetup = false, closeSetup = false; };
  void configure(uint32_t now, bool credentials) {
    hasCredentials_ = credentials;
    connected_ = false;
    disconnectedAt_ = attemptedAt_ = now;
    retryInterval_ = 10000;
    setupAllowed_ = true;
    if (setup_) setupAt_ = now;
  }
  void requestSetup() { setupAllowed_ = true; requested_ = true; }
  void setupFailed() { setup_ = false; setupAllowed_ = false; }
  Actions tick(uint32_t now, bool connected) {
    Actions actions;
    if (connected != connected_) {
      connected_ = connected;
      if (connected) connectedAt_ = now;
      else disconnectedAt_ = attemptedAt_ = now;
    }
    if (connected) retryInterval_ = 10000;
    else if (hasCredentials_ && elapsed(now, attemptedAt_, retryInterval_)) {
      actions.retry = true;
      attemptedAt_ = now;
      retryInterval_ = retryInterval_ < 30000 ? retryInterval_ * 2 : 60000;
    }
    if (setup_ && (elapsed(now, setupAt_, 600000) ||
                   (connected && elapsed(now, connectedAt_, 60000)))) {
      actions.closeSetup = true;
      setup_ = false; setupAllowed_ = false;
    }
    if (!setup_ && setupAllowed_ && (requested_ || !hasCredentials_ ||
                                    (!connected && elapsed(now, disconnectedAt_, 30000)))) {
      actions.openSetup = true;
      setup_ = true; setupAt_ = now;
      // Explicit requests while connected get a fresh grace period.
      if (connected) connectedAt_ = now;
    }
    requested_ = false;
    return actions;
  }
  bool setupActive() const { return setup_; }
 private:
  bool hasCredentials_ = false, connected_ = false;
  bool setup_ = false, setupAllowed_ = true, requested_ = false;
  uint32_t disconnectedAt_ = 0, connectedAt_ = 0, attemptedAt_ = 0, setupAt_ = 0;
  uint32_t retryInterval_ = 10000;
};
}
