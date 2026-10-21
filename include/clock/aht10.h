#pragma once
#include "clock/core.h"

namespace clockapp {
// Bus returns false on NACK, short read or transport timeout. Each tick performs
// at most one transaction. A permanently busy/disconnected sensor cannot stall
// the clock. AHT10 protocol only; this is not an AHT20/30 adapter.
template <class Bus> class Aht10 {
 public:
  explicit Aht10(Bus& bus) : bus_(bus) {}
  void begin(uint32_t now) {
    state_ = State::powerUp; since_ = now; polling_ = false;
    environment_ = {};
  }
  void tick(uint32_t now) {
    uint8_t data[6]{};
    switch (state_) {
      case State::powerUp:
        if (elapsed(now, since_, 40)) {
          const uint8_t init[] = {0xe1, 0x08, 0x00};
          if (!bus_.write(init, sizeof(init))) { fail(now); break; }
          state_ = State::calibrating; since_ = now;
        }
        break;
      case State::calibrating:
        if (!elapsed(now, since_, 100)) break;
        if (!bus_.read(data, 1) || (data[0] & 0x80) || !(data[0] & 0x08)) { fail(now); break; }
        state_ = State::trigger;
        break;
      case State::trigger: {
        const uint8_t command[] = {0xac, 0x33, 0x00};
        if (!bus_.write(command, sizeof(command))) { fail(now); break; }
        state_ = State::measuring; since_ = now;
        break;
      }
      case State::measuring:
        if (!elapsed(now, since_, 80)) break;
        if (elapsed(now, since_, 250)) { fail(now); break; }
        if (polling_ && !elapsed(now, polledAt_, 10)) break;
        polledAt_ = now; polling_ = true;
        if (!bus_.read(data, sizeof(data))) { fail(now); break; }
        if (data[0] & 0x80) break;
        if (!(data[0] & 0x08)) { fail(now); break; }
        {
          const uint32_t h = (uint32_t(data[1]) << 12) | (uint32_t(data[2]) << 4) | (data[3] >> 4);
          const uint32_t t = (uint32_t(data[3] & 0x0f) << 16) | (uint32_t(data[4]) << 8) | data[5];
          if (!environment_.accept(t * 200.0f / 1048576.0f - 50.0f,
                                   h * 100.0f / 1048576.0f, now)) { fail(now); break; }
        }
        state_ = State::idle; since_ = now; polling_ = false;
        break;
      case State::idle:
        if (elapsed(now, since_, 5000)) state_ = State::trigger;
        break;
      case State::retry:
        if (elapsed(now, since_, 10000)) {
          const uint8_t reset[] = {0xba};
          if (!bus_.write(reset, sizeof(reset))) { fail(now); break; }
          state_ = State::powerUp; since_ = now;
        }
        break;
    }
  }
  const Environment& environment() const { return environment_; }
 private:
  enum class State { powerUp, calibrating, trigger, measuring, idle, retry };
  void fail(uint32_t now) {
    environment_.healthy = false;
    state_ = State::retry; since_ = now; polling_ = false;
  }
  Bus& bus_;
  Environment environment_;
  State state_ = State::powerUp;
  uint32_t since_ = 0, polledAt_ = 0;
  bool polling_ = false;
};
}
