#pragma once

#if defined(ARDUINO_ARCH_ESP8266)
#include <Arduino.h>
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

namespace clockapp {
#if defined(ARDUINO_ARCH_ESP8266)
// Portal, MQTT and weather all run from loop(); SDK callbacks don't touch
// their state. A yield services the SDK without reentering loop().
struct StateMutex { void lock() {} void unlock() {} };
struct WriteMutex {
  bool begin() { return true; }
  bool take(unsigned = 0) { return true; }
  void give() {}
};
template <typename T> class StateValue {
 public:
  constexpr StateValue(T value) : value_(value) {}
  T load() const { return value_; }
  void operator=(T value) { value_ = value; }
  void operator++() { ++value_; }
  void fetch_add(T value) { value_ += value; }
 private:
  T value_;
};
#else
struct StateMutex {
  void lock() { portENTER_CRITICAL(&mux); }
  void unlock() { portEXIT_CRITICAL(&mux); }
 private:
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
};
struct WriteMutex {
  bool begin() { handle = xSemaphoreCreateMutex(); return handle != nullptr; }
  bool take(unsigned ms = 0) { return handle && xSemaphoreTake(handle, pdMS_TO_TICKS(ms)) == pdTRUE; }
  void give() { xSemaphoreGive(handle); }
 private:
  SemaphoreHandle_t handle = nullptr;
};
template <typename T> using StateValue = std::atomic<T>;
#endif
}
