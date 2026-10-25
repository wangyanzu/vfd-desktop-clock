#pragma once
#include "clock/core.h"

namespace clockapp {
// Every tick performs at most one bounded bus operation. Recovery belongs to
// this state machine; diagnostics never generate extra sensor transactions.
template <class Bus> class Aht10 {
 public:
  enum class Error { none, write, status, calibration, busy, read, empty, range, bus };
  struct Diagnostics {
    uint32_t samples=0, failures=0, ioErrors=0, emptyFrames=0, resets=0, recoveries=0;
    uint32_t consecutiveFailures=0;
  };
  explicit Aht10(Bus& bus):bus_(bus) {}
  void begin(uint32_t now) {
    environment_={}; diagnostics_={}; lastReading_={}; hasReading_=false; error_=Error::none;
    restart(now);
  }
  void resume(uint32_t now) { environment_.healthy=false; restart(now); }
  void tick(uint32_t now) {
    uint8_t data[6]{};
    switch (state_) {
      case State::powerUp:
        if (elapsed(now,since_,100)) state_=State::reset;
        break;
      case State::reset: {
        const uint8_t reset[]={0xba};
        if (!bus_.write(reset,1)) { ++diagnostics_.ioErrors; fail(now,Error::write); break; }
        ++diagnostics_.resets; state_=State::resetWait; since_=now;
        break;
      }
      case State::resetWait:
        if (elapsed(now,since_,40)) { state_=State::status; since_=now; polling_=false; }
        break;
      case State::status:
        if (!pollDue(now,30)) break;
        if (!bus_.read(data,1)) {
          ++diagnostics_.ioErrors;
          if (elapsed(now,since_,500)) fail(now,Error::status);
          break;
        }
        if (data[0]&0x80) { if (elapsed(now,since_,500)) fail(now,Error::busy); break; }
        if ((data[0]&0x08) && !(diagnostics_.consecutiveFailures &&
            (error_==Error::empty || error_==Error::range || error_==Error::calibration))) {
          attempts_=0; state_=State::trigger;
        }
        else state_=State::initialize;
        break;
      case State::initialize: {
        const uint8_t init[]={0xe1,0x08,0};
        if (!bus_.write(init,3)) ++diagnostics_.ioErrors;
        state_=State::calibrating; since_=now; polling_=false;
        break;
      }
      case State::calibrating:
        if (!elapsed(now,since_,350) || !pollDue(now,30)) break;
        if (!bus_.read(data,1)) {
          ++diagnostics_.ioErrors;
          if (elapsed(now,since_,1000)) fail(now,Error::status);
          break;
        }
        if (!(data[0]&0x80) && (data[0]&0x08)) { attempts_=0; state_=State::trigger; }
        else if (elapsed(now,since_,1000)) fail(now,Error::calibration);
        break;
      case State::trigger: {
        const uint8_t command[]={0xac,0x33,0};
        ++attempts_;
        if (!bus_.write(command,3)) { ++diagnostics_.ioErrors; retryConversion(now,Error::write); break; }
        state_=State::measuring; since_=now; polling_=false;
        break;
      }
      case State::measuring:
        // AHT10 humidity conversion can need longer than its nominal 75–80ms.
        if (!elapsed(now,since_,120) || !pollDue(now,30)) break;
        if (!bus_.read(data,1)) {
          ++diagnostics_.ioErrors;
          if (elapsed(now,since_,500)) retryConversion(now,Error::status);
          break;
        }
        if (data[0]&0x80) { if (elapsed(now,since_,500)) retryConversion(now,Error::busy); break; }
        if (!(data[0]&0x08)) { fail(now,Error::calibration); break; }
        state_=State::reading;
        break;
      case State::reading:
        if (!bus_.read(data,6)) {
          ++diagnostics_.ioErrors;
          retryRead(now,Error::read);
          break;
        }
        std::memcpy(lastReading_.data(),data,6); hasReading_=true;
        if (data[0]&0x80) { retryRead(now,Error::busy); break; }
        if (!(data[0]&0x08)) { fail(now,Error::calibration); break; }
        if (!(data[1] || data[2] || data[3] || data[4] || data[5])) {
          ++diagnostics_.emptyFrames; retryRead(now,Error::empty); break;
        }
        {
          const uint32_t h=(uint32_t(data[1])<<12)|(uint32_t(data[2])<<4)|(data[3]>>4);
          const uint32_t t=(uint32_t(data[3]&15)<<16)|(uint32_t(data[4])<<8)|data[5];
          const float temperature=t*200.0f/1048576.0f-50.0f, humidity=h*100.0f/1048576.0f;
          if (!std::isfinite(temperature) || temperature < -40 || temperature > 85 || humidity < 0 || humidity > 100) {
            retryRead(now,Error::range); break;
          }
          environment_.accept(temperature,humidity,now);
        }
        ++diagnostics_.samples; diagnostics_.consecutiveFailures=0;
        error_=Error::none; state_=State::idle; since_=now;
        break;
      case State::readWait:
        if (elapsed(now,polledAt_,30)) state_=State::measuring;
        break;
      case State::conversionWait:
        if (elapsed(now,since_,100)) state_=State::trigger;
        break;
      case State::idle:
        if (elapsed(now,since_,5000)) { attempts_=0; state_=State::trigger; }
        break;
      case State::retry:
        if (elapsed(now,since_,retryDelayMs())) state_=diagnostics_.consecutiveFailures>=3?State::recover:State::reset;
        break;
      case State::recover:
        ++diagnostics_.recoveries;
        if (!bus_.recover()) { fail(now,Error::bus); break; }
        state_=State::powerUp; since_=now; polling_=false;
        break;
    }
  }
  Error errorCode() const { return error_; }
  const char* error() const {
    switch (error_) {
      case Error::none:return nullptr; case Error::write:return "I2C write failed";
      case Error::status:return "status read failed"; case Error::calibration:return "calibration failed";
      case Error::busy:return "measurement busy timeout"; case Error::read:return "measurement read failed";
      case Error::empty:return "empty measurement after 3 conversions";
      case Error::range:return "measurement out of range"; case Error::bus:return "I2C bus recovery failed";
    }
    return "sensor error";
  }
  uint32_t retryDelayMs() const {
    const auto failures=diagnostics_.consecutiveFailures;
    return failures<=1?2000:failures==2?4000:failures==3?8000:failures==4?16000:30000;
  }
  bool hasReading() const { return hasReading_; }
  const std::array<uint8_t,6>& lastReading() const { return lastReading_; }
  const Diagnostics& diagnostics() const { return diagnostics_; }
  const Environment& environment() const { return environment_; }
 private:
  enum class State { powerUp, reset, resetWait, status, initialize, calibrating, trigger, measuring, reading,
                     readWait, conversionWait, idle, retry, recover };
  void restart(uint32_t now) { state_=State::powerUp; since_=now; polling_=false; attempts_=0; }
  bool pollDue(uint32_t now,uint32_t interval) {
    if (polling_ && !elapsed(now,polledAt_,interval)) return false;
    polling_=true; polledAt_=now; return true;
  }
  void retryRead(uint32_t now,Error error) {
    if (elapsed(now,since_,500)) retryConversion(now,error);
    else { state_=State::readWait; polledAt_=now; }
  }
  void retryConversion(uint32_t now,Error error) {
    if (attempts_>=3) fail(now,error);
    else { state_=State::conversionWait; since_=now; polling_=false; }
  }
  void fail(uint32_t now,Error error) {
    ++diagnostics_.failures; ++diagnostics_.consecutiveFailures;
    environment_.healthy=false; error_=error;
    state_=State::retry; since_=now; polling_=false; attempts_=0;
  }
  Bus& bus_;
  Environment environment_;
  Diagnostics diagnostics_;
  Error error_=Error::none;
  std::array<uint8_t,6> lastReading_{};
  State state_=State::powerUp;
  uint32_t since_=0,polledAt_=0;
  uint8_t attempts_=0;
  bool polling_=false,hasReading_=false;
};
}
