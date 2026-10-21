#include "clock/network.h"
#include <Arduino.h>

namespace clockapp {
void NetworkManager::begin(const Settings& settings, const char* accessKey, uint32_t now) {
  std::snprintf(accessKey_, sizeof(accessKey_), "%s", accessKey);
  const uint32_t suffix = static_cast<uint32_t>(ESP.getEfuseMac() >> 24) & 0xffffff;
  std::snprintf(apName_, sizeof(apName_), "VFD-Clock-%06lX", static_cast<unsigned long>(suffix));
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  connect(settings, now);
}
void NetworkManager::connect(const Settings& settings, uint32_t now) {
  active_ = settings;
  policy_.configure(now, active_.ssid[0] != '\0');
  WiFi.disconnect(false, false);
  if (active_.ssid[0]) WiFi.begin(active_.ssid, active_.password);
}
void NetworkManager::tick(const Settings& desired, uint32_t now) {
  if (!sameNetwork(desired, active_)) {
    if (!waiting_ || !sameNetwork(desired, pending_)) {
      pending_ = desired; changedAt_ = now; waiting_ = true;
    }
  } else waiting_ = false;
  // Let the HTTP response leave before changing the client's network.
  if (waiting_ && elapsed(now, changedAt_, 1500)) {
    waiting_ = false; connect(pending_, now);
  }
  const bool online = connected();
  if (online != wasConnected_) {
    wasConnected_ = online;
    if (online) Serial.printf("WiFi connected. Settings: http://%s\n", WiFi.localIP().toString().c_str());
    else Serial.println("WiFi disconnected.");
  }
  const auto actions = policy_.tick(now, online);
  if (actions.retry) {
    WiFi.disconnect(false, false);
    WiFi.begin(active_.ssid, active_.password);
  }
  if (actions.openSetup) {
    if (!WiFi.mode(WIFI_AP_STA) || !WiFi.softAP(apName_, accessKey_)) {
      policy_.setupFailed(); Serial.println("Could not open setup hotspot.");
    } else Serial.printf("Setup hotspot: %s, address: http://%s\n", apName_, WiFi.softAPIP().toString().c_str());
  }
  if (actions.closeSetup) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    Serial.println("Setup hotspot closed.");
  }
}
}
