#pragma once
#include <Preferences.h>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "clock/settings.h"
#include "clock/mqtt_settings.h"
#include "clock/rotation.h"

namespace clockapp {
struct DeviceStatus {
  bool connected = false, setupActive = false, sensorEnabled = false, sampleFresh = false;
  bool sampleValid = false, timeValid = false;
  float temperature = 0, humidity = 0;
  char ip[16]{};
  char clock[9] = "--:--:--";
  uint32_t uptimeSeconds = 0;
  char mqttState[16] = "disabled";
  uint32_t messageId = 0, messagesDropped = 0, messagesDuplicate = 0;
  uint8_t messageCount = 0;
  bool messageVisible = false, rotationPaused = true;
  char contentPage[16] = "date";
};
class ConfigPortal {
 public:
  bool begin(const Settings& defaults, const MqttSettings& mqttDefaults = MqttSettings{});
  bool start();
  Settings settings();
  MqttSettings mqttSettings();
  RotationSettings rotationSettings();
  void publish(const DeviceStatus& status);
  bool cycleBrightness();
  bool takeMessageAck(uint32_t& id);
  const char* accessKey() const { return accessKey_; }
 private:
  enum class SaveResult { ok, unavailable };
  SaveResult saveLocked(const Settings& next);
  static esp_err_t dispatch(httpd_req_t* request);
  esp_err_t handle(httpd_req_t* request);
  bool authorized(httpd_req_t* request);
  bool csrfValid(httpd_req_t* request);
  esp_err_t send(httpd_req_t* request, const char* type, const char* body);
  esp_err_t error(httpd_req_t* request, const char* status, const char* message);
  std::string stateJson();
  esp_err_t update(httpd_req_t* request);
  esp_err_t updateRotation(httpd_req_t* request);
  esp_err_t updateMqtt(httpd_req_t* request);
  bool receiveForm(httpd_req_t* request, std::string& body, size_t maximum);
  esp_err_t acknowledgeMessage(httpd_req_t* request);
  uint32_t pendingMessageAck_ = 0;

  Preferences storage_;
  SemaphoreHandle_t writer_ = nullptr;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  Settings settings_{};
  MqttSettings mqttSettings_{};
  RotationSettings rotationSettings_{};
  DeviceStatus status_{};
  bool storageReady_ = false;
  httpd_handle_t server_ = nullptr;
  char accessKey_[17]{};
  char csrf_[33]{};
  char authorization_[80]{};
};
}
