#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "clock/frame.h"

namespace clockapp {
// All intervals must be shorter than one full uint32_t tick cycle.
inline bool elapsed(uint32_t now, uint32_t since, uint32_t interval) {
  return static_cast<uint32_t>(now - since) >= interval;
}

struct Environment {
  float temperature = 0;
  float humidity = 0;
  uint32_t sampledAt = 0;
  bool valid = false;
  bool healthy = false;

  bool accept(float t, float h, uint32_t now) {
    if (!std::isfinite(t) || !std::isfinite(h) || t < -40 || t > 85 || h < 0 || h > 100) {
      healthy = false;
      return false;
    }
    temperature = t;
    humidity = h;
    sampledAt = now;
    valid = healthy = true;
    return true;
  }
  bool fresh(uint32_t now) const {
    return valid && healthy && !elapsed(now, sampledAt, 15000);
  }
};

inline Frame render(const tm* localTime, const Environment& environment,
                    bool sensorEnabled, bool connected, uint32_t now) {
  Frame frame;
  char buffer[48];
  if (localTime) {
    std::strftime(buffer, sizeof(buffer), "%m-%d %H:%M:%S", localTime);
    frame.set(0, buffer);
    frame.lines[0][19] = connected ? ' ' : '!';
  } else {
    frame.set(0, connected ? "Syncing time..." : "WiFi offline");
  }
  if (!sensorEnabled) {
    frame.set(1, "AHT10 pins not set");
  } else if (!environment.valid) {
    frame.set(1, "T:--.-C RH:--.-%");
  } else {
    std::snprintf(buffer, sizeof(buffer), "T:%.1fC RH:%.1f%%", environment.temperature,
                  environment.humidity);
    frame.set(1, buffer);
    if (!environment.fresh(now)) frame.lines[1][19] = '*';
  }
  return frame;
}

// Display protocol lives in vfd.h; kept available to existing callers.
class Button {
 public:
  enum class Event { none, shortPress, longPress };
  Event update(bool pressed, uint32_t now) {
    if (pressed != raw_) { raw_ = pressed; changedAt_ = now; }
    if (stable_ != raw_ && elapsed(now, changedAt_, 30)) {
      stable_ = raw_;
      if (stable_) { pressedAt_ = now; longSent_ = false; }
      else if (!longSent_) {
        // A delayed polling cycle must not turn a long hold into a short click.
        return elapsed(now, pressedAt_, 3000) ? Event::longPress : Event::shortPress;
      }
    }
    if (stable_ && !longSent_ && elapsed(now, pressedAt_, 3000)) {
      longSent_ = true;
      return Event::longPress;
    }
    return Event::none;
  }
 private:
  bool raw_ = false, stable_ = false, longSent_ = false;
  uint32_t changedAt_ = 0, pressedAt_ = 0;
};
}

#include "clock/vfd.h"
