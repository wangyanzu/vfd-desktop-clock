#pragma once
#include <WiFi.h>
#include "clock/network_policy.h"
#include "clock/settings.h"

namespace clockapp {
class NetworkManager {
 public:
  void begin(const Settings& settings, const char* accessKey, uint32_t now);
  void tick(const Settings& desired, uint32_t now);
  void requestSetup() { policy_.requestSetup(); }
  bool setupActive() const { return policy_.setupActive(); }
  bool connected() const { return WiFi.status() == WL_CONNECTED; }
 private:
  void connect(const Settings& settings, uint32_t now);
  NetworkPolicy policy_;
  Settings active_{}, pending_{};
  bool waiting_ = false, wasConnected_ = false;
  uint32_t changedAt_ = 0;
  char accessKey_[17]{}, apName_[32]{};
};
}
