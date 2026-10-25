#pragma once
#include "clock/weather_settings.h"
#include "clock/weather.h"
#include <atomic>
#include <freertos/FreeRTOS.h>

namespace clockapp {
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
  void pause(bool paused) { pauseRequested_=paused; }
  bool paused() const { return !started_ || paused_.load(); }
 private:
  static void task(void* self);
  void run();
  WeatherSettings desired_{};
  WeatherSnapshot snapshot_{};
  uint32_t generation_=0;
  portMUX_TYPE mux_=portMUX_INITIALIZER_UNLOCKED;
  bool started_=false;
  std::atomic<bool> pauseRequested_{false},paused_{false};
};
}
