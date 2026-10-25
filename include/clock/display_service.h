#pragma once
#include "clock/animation.h"
#include "clock/vfd.h"

namespace clockapp {
// Port::drained() must mean software TX queue AND hardware FIFO are empty.
// No waits, allocation or sensor dependencies; all times use uint32_t ticks.
template <class Port> class DisplayService {
 public:
  enum class State { boot, awake, closing, asleep, waking };
  explicit DisplayService(Port& port, Motion motion = Motion::standard, uint32_t wakeSettleMs = 100, bool centerContent = false)
      : port_(port), driver_(port), motion_(motion), wakeSettleMs_(wakeSettleMs), centerContent_(centerContent) {}
  bool begin(uint8_t brightness) {
    if (brightness < 1 || brightness > 4) return false;
    desiredBrightness_ = brightness; enabled_ = true;
    return true;
  }
  void brightness(uint8_t value) { if (value >= 1 && value <= 4) desiredBrightness_ = value; }
  void page(const CellFrame& frame, uint32_t key, uint32_t now, bool firstContent = false, uint8_t transitionRows = 2, int8_t offset = 0) {
    const bool changed = hasPage_ && key != pageKey_;
    latest_ = centerContent_ ? centeredFrame(frame, offset) : frame; pageKey_ = key; hasPage_ = true;
    if (!initialized_ || state_ != State::awake) return;
    if (changed) {
      const bool message = key >= 16;
      const Effect effect = motion_ == Motion::standard ? (message ? Effect::messageEntry : Effect::terminalSlide) : Effect::pushLeft;
      animator_.start(effect, latest_, now, duration(message ? 900 : 640, 160), message ? 2 : transitionRows);
      haveSent_ = false;
    }
    else if (firstContent && !animator_.active(now)) animator_.start(Effect::revealFields, latest_, now, duration(320, 120), 2);
    else animator_.target(latest_); // coalesce data; never restart the timeline
  }
  void sleep(uint32_t now) {
    if (!initialized_ || state_ == State::closing || state_ == State::asleep) return;
    if (state_ == State::waking && !wakeSent_) { state_ = State::asleep; return; }
    state_ = State::closing; haveSent_ = false;
    animator_.start(Effect::collapse, CellFrame{}, now, duration(480, 160));
  }
  void wake(uint32_t now) {
    if (!initialized_) return;
    if (state_ == State::asleep) { state_ = State::waking; wakeSent_ = false; wakeDrained_ = false; }
    else if (state_ == State::closing) {
      state_ = State::awake; haveSent_ = false;
      animator_.start(Effect::centerReveal, latest_, now, duration(400, 80));
    }
  }
  State state() const { return state_; }
  bool ready() const { return initialized_; }
  bool settled(uint32_t now) const { return initialized_ && state_ == State::awake && !animator_.active(now) && port_.drained(); }
  bool pageVisible(uint32_t key, uint32_t now) const { return hasVisiblePage_ && visiblePageKey_ == key && settled(now); }
  bool asleepAndDrained() const { return initialized_ && state_ == State::asleep && port_.drained(); }
  const CellFrame& visible() const { return animator_.visible(); }

  void tick(uint32_t now) {
    if (!enabled_ || !port_.drained()) return;
    if (!initialized_) {
      if (!driver_.begin(desiredBrightness_)) return;
      hasVisiblePage_ = false;
      initialized_ = true; appliedBrightness_ = desiredBrightness_;
      sceneAt_ = now;
      if (motion_ == Motion::off) {
        state_ = State::awake;
        animator_.start(Effect::replace, latest_, now, 0);
      } else {
        state_ = State::boot;
        CellFrame title;
        title.setAscii(0, "     VFD CLOCK");
        title.setAscii(1, "    A LITTLE TIME");
        animator_.start(Effect::centerReveal, centerContent_ ? centeredFrame(title) : title, now, duration(800, 200));
      }
      return;
    }
    if (state_ == State::asleep) return;
    if (state_ == State::waking) {
      if (!wakeSent_) {
        // Reassert volatile settings and invalidate the driver's frame cache.
        // No assumption about display memory surviving display power-off.
        if (!driver_.begin(desiredBrightness_)) return;
        hasVisiblePage_ = false;
        appliedBrightness_ = desiredBrightness_;
        wakeSent_ = true; sceneAt_ = now;
        return;
      }
      if (!wakeDrained_) { wakeDrained_ = true; sceneAt_ = now; }
      if (!elapsed(now, sceneAt_, wakeSettleMs_)) return;
      state_ = State::awake;
      animator_.start(Effect::centerReveal, latest_, now, duration(400, 80));
      haveSent_ = false; // show current time on the first waking frame
    }
    if (desiredBrightness_ != appliedBrightness_) {
      if (driver_.setBrightness(desiredBrightness_)) appliedBrightness_ = desiredBrightness_;
      return;
    }
    if (state_ == State::boot && elapsed(now, sceneAt_, duration(1000, 300))) {
      state_ = State::awake;
      animator_.start(Effect::reveal, latest_, now, duration(320, 120));
    }
    if (state_ == State::closing && !animator_.active(now) && animator_.visible() == CellFrame{}) {
      // The last blank frame has actually drained, not just entered TX memory.
      if (driver_.setPower(false)) state_ = State::asleep;
      return;
    }
    if (haveSent_ && !elapsed(now, sentAt_, motion_ == Motion::standard ? 40 : 100)) return;
    CellFrame next = animator_.sample(now);
    if (!driver_.show(next)) return;
    animator_.committed(next); sentAt_ = now; haveSent_ = true;
    if (state_ == State::awake && !animator_.active(now)) { visiblePageKey_ = pageKey_; hasVisiblePage_ = true; }
  }
 private:
  uint32_t duration(uint32_t normal, uint32_t reduced) const {
    return motion_ == Motion::off ? 0 : (motion_ == Motion::reduced ? reduced : normal);
  }
  Port& port_;
  Vfd<Port> driver_;
  Animator animator_;
  CellFrame latest_;
  Motion motion_;
  State state_ = State::boot;
  uint32_t wakeSettleMs_, sceneAt_ = 0, sentAt_ = 0, pageKey_ = 0, visiblePageKey_ = 0;
  uint8_t desiredBrightness_ = 2, appliedBrightness_ = 2;
  bool centerContent_ = false;
  bool hasVisiblePage_ = false;
  bool enabled_ = false, initialized_ = false, haveSent_ = false, hasPage_ = false, wakeSent_ = false, wakeDrained_ = false;
};
}
