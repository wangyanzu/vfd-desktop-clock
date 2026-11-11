#pragma once
#include "clock/runtime.h"
#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WebServer.h>
#include "clock/esp8266_storage.h"
#else
#include <Preferences.h>
#include <esp_http_server.h>
#endif
#include "clock/settings.h"
#include "clock/mqtt_settings.h"
#include "clock/rotation.h"
#include "clock/weather_settings.h"
#include "clock/weather.h"

namespace clockapp {
#if defined(ARDUINO_ARCH_ESP8266)
struct PortalRequest {
  ESP8266WebServer& server;
  const char* uri;
  int status = 200;
};
using PortalResult = int;
#else
using PortalRequest = httpd_req_t;
using PortalResult = esp_err_t;
#endif
struct DeviceStatus {
  bool connected = false, setupActive = false, sensorEnabled = false, sampleFresh = false;
  bool sampleValid = false, timeValid = false;
  uint8_t sensorError = 0;
  uint32_t sensorSamples = 0, sensorFailures = 0, sensorConsecutiveFailures = 0, sensorRecoveries = 0;
  float temperature = 0, humidity = 0;
  char ip[16]{};
  char clock[9] = "--:--:--";
  uint32_t uptimeSeconds = 0;
  char mqttState[16] = "disabled";
  uint32_t messageId = 0, messagesDropped = 0, messagesDuplicate = 0, messagesEvicted = 0;
  uint8_t messageCount = 0, messageLevel = 0;
  bool messageVisible = false, rotationPaused = true;
  char contentPage[16] = "date";
  OutdoorWeather outdoor;
  char weatherState[24] = "disabled";
  int weatherHttpStatus = 0;
  char weatherErrorCode[12]{};
};
class ConfigPortal {
 public:
  bool begin(const Settings& defaults, const MqttSettings& mqttDefaults = MqttSettings{});
  bool start();
#if defined(ARDUINO_ARCH_ESP8266)
  void poll() { server_.handleClient(); }
#else
  bool localStage(const char*,const char*,const char*);
  bool localCancel();
  bool localClear();
  void localTick(bool connected);
#endif
  Settings settings();
  MqttSettings mqttSettings();
  RotationSettings rotationSettings();
  WeatherSettings weatherSettings();
  void publish(const DeviceStatus& status);
  bool cycleBrightness();
  bool takeMessageAck(uint32_t& id);
  bool sensorValid() { mux_.lock(); bool valid=status_.sampleValid; mux_.unlock(); return valid; }
  const char* accessKey() const { return accessKey_; }
 private:
  enum class SaveResult { ok, unavailable };
  SaveResult saveLocked(const Settings& next);
#if !defined(ARDUINO_ARCH_ESP8266)
  static PortalResult dispatch(PortalRequest* request);
#endif
  PortalResult handle(PortalRequest* request);
  bool authorized(PortalRequest* request);
  bool csrfValid(PortalRequest* request);
  PortalResult send(PortalRequest* request, const char* type, const char* body);
  PortalResult error(PortalRequest* request, const char* status, const char* message);
  std::string stateJson();
  PortalResult update(PortalRequest* request);
  PortalResult updateRotation(PortalRequest* request);
  PortalResult updateMqtt(PortalRequest* request);
  PortalResult updateWeather(PortalRequest* request);
  bool receiveForm(PortalRequest* request, std::string& body, size_t maximum);
  PortalResult acknowledgeMessage(PortalRequest* request);
  uint32_t pendingMessageAck_ = 0;

#if defined(ARDUINO_ARCH_ESP8266)
  FlashSettings storage_;
  ESP8266WebServer server_{80};
#else
  Preferences storage_;
  httpd_handle_t server_ = nullptr;
#endif
  WriteMutex writer_;
  StateMutex mux_;
  Settings settings_{};
  MqttSettings mqttSettings_{};
  RotationSettings rotationSettings_{};
  WeatherSettings weatherSettings_{};
  DeviceStatus status_{};
  bool storageReady_ = false;
  char accessKey_[17]{};
  char csrf_[33]{};
  char authorization_[80]{};
};
}
