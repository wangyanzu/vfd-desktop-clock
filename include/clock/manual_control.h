#pragma once
#include "clock/core.h"

namespace clockapp {
class SleepPreparation {
 public:
  enum class Result { waiting, ready, timedOut };
  void begin(uint32_t now) { startedAt_ = now; releasing_ = false; }
  Result poll(uint32_t now, bool pressed, bool displayDrained, bool mqttPaused) {
    if (elapsed(now, startedAt_, 15000)) return Result::timedOut;
    if (pressed) releasing_ = false;
    else if (!releasing_) { releasing_ = true; releasedAt_ = now; }
    if (!releasing_ || !elapsed(now, releasedAt_, 30)) return Result::waiting;
    return displayDrained && mqttPaused ? Result::ready : Result::waiting;
  }
 private:
  uint32_t startedAt_ = 0, releasedAt_ = 0;
  bool releasing_ = false;
};
// The wake press belongs only to waking: ignore it until a debounced release.
class ManualControl {
 public:
  Button::Event update(bool pressed, uint32_t now, bool confirmationAvailable = false) {
    if (ignoreWakePress_) {
      if (pressed) releaseTiming_ = false;
      else if (!releaseTiming_) { releaseTiming_ = true; releasedAt_ = now; }
      else if (elapsed(now, releasedAt_, 30)) {
        ignoreWakePress_ = false;
        button_ = Button{};
      }
      return Button::Event::none;
    }
    if (pressed && confirmationAvailable && !sleeping_) confirmPress_ = true;
    const auto event = button_.update(pressed, now);
    if (event == Button::Event::shortPress) {
      lastShortConfirms_ = confirmPress_; confirmPress_ = false;
      if (!lastShortConfirms_) sleeping_ = !sleeping_;
    }
    if (event == Button::Event::longPress) confirmPress_ = false;
    return event;
  }
  void sleep() { sleeping_ = true; }
  void wake(uint32_t now) {
    sleeping_ = false; confirmPress_ = false; ignoreWakePress_ = true; releaseTiming_ = false; releasedAt_ = now;
  }
  bool sleeping() const { return sleeping_; }
  bool confirmationPress() const { return lastShortConfirms_; }
 private:
  Button button_;
  bool confirmPress_ = false, lastShortConfirms_ = false;
  bool sleeping_ = false, ignoreWakePress_ = false, releaseTiming_ = false;
  uint32_t releasedAt_ = 0;
};
}
