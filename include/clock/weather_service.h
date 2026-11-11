#pragma once
#include "clock/weather_settings.h"
#include "clock/weather.h"
#include "clock/runtime.h"

namespace clockapp {
class MqttService;
enum class WeatherState { disabled, waitingWifi, waitingTime, fetching, online, retrying, invalidResponse, authError, rateLimited, unavailable };
inline const char* weatherStateName(WeatherState state) {
  switch(state) {
    case WeatherState::disabled:return "disabled"; case WeatherState::waitingWifi:return "waiting_wifi";
    case WeatherState::waitingTime:return "waiting_time"; case WeatherState::fetching:return "fetching";
    case WeatherState::online:return "online"; case WeatherState::retrying:return "retrying";
    case WeatherState::invalidResponse:return "invalid_response"; case WeatherState::authError:return "auth_error";
    case WeatherState::rateLimited:return "rate_limited"; default:return "unavailable";
  }
}
struct WeatherSnapshot { OutdoorWeather observation; WeatherState state=WeatherState::disabled; int httpStatus=0; char errorCode[12]{}; };
class WeatherService {
 public:
  bool begin(const WeatherSettings& settings);
  void configure(const WeatherSettings& settings);
  WeatherSnapshot snapshot();
#if defined(ARDUINO_ARCH_ESP8266)
  void poll(MqttService& mqtt);
#endif
  void pause(bool paused) { pauseRequested_=paused; }
  bool paused() const { return !started_ || paused_.load(); }
 private:
  void tick(MqttService* mqtt);
#if !defined(ARDUINO_ARCH_ESP8266)
  static void task(void* self);
  void run();
#endif
  uint32_t activeGeneration_=0,lastAttempt_=0,interval_=0;
  bool scheduled_=false,wasOnline_=false;
  WeatherSettings desired_{};
  WeatherSnapshot snapshot_{};
  uint32_t generation_=0;
  StateMutex mux_;
  bool started_=false;
  StateValue<bool> pauseRequested_{false},paused_{false};
};
}
