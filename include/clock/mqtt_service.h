#pragma once
#include <array>
#include "clock/runtime.h"
#include "clock/pending_messages.h"
#include "clock/mqtt_settings.h"

namespace clockapp {
enum class MqttState : uint8_t { disabled, invalid, waitingWifi, waitingTime, connecting, online, retrying, unavailable };
inline const char* mqttStateName(MqttState state) {
  switch (state) {
    case MqttState::disabled: return "disabled";
    case MqttState::invalid: return "invalid";
    case MqttState::waitingWifi: return "waiting_wifi";
    case MqttState::waitingTime: return "waiting_time";
    case MqttState::connecting: return "connecting";
    case MqttState::online: return "online";
    case MqttState::retrying: return "retrying";
    case MqttState::unavailable: return "unavailable";
  }
  return "unavailable";
}
class MqttService {
 public:
  bool begin(const MqttSettings& settings);
  void configure(const MqttSettings& settings);
  bool receive(IncomingMessage& message);
#if defined(ARDUINO_ARCH_ESP8266)
  void poll();
  void releaseTls();
#endif
  MqttState state() const { return state_.load(); }
  uint32_t dropped() const { return dropped_.load(); }
  uint32_t duplicates() const { return duplicates_.load(); }
  uint32_t evicted() const { return evicted_.load(); }
  void pause(bool value) { pauseRequested_ = value; }
  bool paused() const { return paused_.load() || !started_; }
 private:
  struct Worker;
#if defined(ARDUINO_ARCH_ESP8266)
  Worker* worker_ = nullptr;  // lifetime of the firmware
#else
  static void task(void* self);
  void run();
#endif
  MqttSettings desired_{};
  StateMutex mux_;
  uint32_t generation_ = 0;
  PendingMessages incoming_;
  bool started_ = false;
  StateValue<MqttState> state_{MqttState::disabled};
  StateValue<uint32_t> dropped_{0}, evicted_{0}, duplicates_{0};
  StateValue<bool> pauseRequested_{false}, paused_{false};
};
}
