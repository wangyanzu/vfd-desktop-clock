#include "clock/portal.h"
#include "clock/messages.h"
#include "clock/web_assets.h"
#include <Arduino.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <algorithm>

namespace clockapp {
namespace {
void randomHex(char* output, size_t length) {
  const char hex[] = "0123456789ABCDEF";
  uint8_t bytes[16];
  esp_fill_random(bytes, sizeof(bytes));
  for (size_t i = 0; i < length; ++i) output[i] = hex[(bytes[i / 2] >> ((i % 2) * 4)) & 15];
  output[length] = 0;
}
bool equalSecret(const char* a, const char* b, size_t length) {
  volatile uint8_t difference = 0;
  for (size_t i = 0; i < length; ++i) difference = difference | (uint8_t(a[i]) ^ uint8_t(b[i]));
  return difference == 0;
}
bool validAccessKey(const char* key) {
  if (std::strlen(key) != 8) return false;
  for (size_t i = 0; i < 8; ++i)
    if (key[i] < '0' || key[i] > '9') return false;
  return true;
}
void randomAccessKey(char* output) {
  for (size_t i = 0; i < 8; ++i) {
    uint8_t value;
    // Reject the uneven tail so each digit has the same probability.
    do { esp_fill_random(&value, sizeof(value)); } while (value >= 250);
    output[i] = '0' + value % 10;
  }
  output[8] = 0;
}
}

bool ConfigPortal::begin(const Settings& defaults, const MqttSettings& mqttDefaults) {
  settings_ = defaults; mqttSettings_ = mqttDefaults;
  writer_ = xSemaphoreCreateMutex();
  if (!writer_) return false;
  storageReady_ = storage_.begin("clock", false);
  if (storageReady_) {
    SettingsRecord record{};
    if (storage_.getBytesLength("settings") == record.size() &&
        storage_.getBytes("settings", record.data(), record.size()) == record.size()) {
      if (!decodeSettings(record, settings_)) Serial.println("Invalid settings record; using defaults.");
    } else {
      // Migrate brightness from the first firmware. Network remains in defaults.
      const auto previous = storage_.getUChar("brightness", defaults.brightness);
      if (previous >= 1 && previous <= 4) settings_.brightness = previous;
    }
    MqttSettingsRecord mqttRecord{};
    if (storage_.getBytesLength("mqtt") == mqttRecord.size() &&
        storage_.getBytes("mqtt", mqttRecord.data(), mqttRecord.size()) == mqttRecord.size()) {
      if (!decodeMqttSettings(mqttRecord, mqttSettings_)) {
        mqttSettings_ = MqttSettings{};
        Serial.println("Invalid MQTT record; MQTT disabled.");
      }
    } else if (storage_.getBytesLength("mqtt") != 0) mqttSettings_ = MqttSettings{};
    RotationRecord rotationRecord{};
    if (storage_.getBytesLength("rotation") == rotationRecord.size() &&
        storage_.getBytes("rotation", rotationRecord.data(), rotationRecord.size()) == rotationRecord.size()) {
      if (!decodeRotation(rotationRecord, rotationSettings_)) Serial.println("Invalid rotation record; using defaults.");
    }
    WeatherRecord weatherRecord{};
    if (storage_.getBytesLength("weather") == weatherRecord.size() &&
        storage_.getBytes("weather", weatherRecord.data(), weatherRecord.size()) == weatherRecord.size()) {
      if (!decodeWeather(weatherRecord, weatherSettings_)) Serial.println("Invalid weather record; weather disabled.");
    }
    storage_.getString("access", accessKey_, sizeof(accessKey_));
  }
  if (!validAccessKey(accessKey_)) {
    randomAccessKey(accessKey_);
    if (!storageReady_ || storage_.putString("access", accessKey_) != 8) return false;
  }
  randomHex(csrf_, 32);
  const std::string credentials = std::string("admin:") + accessKey_;
  unsigned char encoded[64]{};
  size_t length = 0;
  if (mbedtls_base64_encode(encoded, sizeof(encoded), &length,
                          reinterpret_cast<const unsigned char*>(credentials.data()), credentials.size()) != 0) return false;
  std::snprintf(authorization_, sizeof(authorization_), "Basic %s", encoded);
  return true;
}

Settings ConfigPortal::settings() {
  portENTER_CRITICAL(&mux_);
  const Settings copy = settings_;
  portEXIT_CRITICAL(&mux_);
  return copy;
}
MqttSettings ConfigPortal::mqttSettings() {
  portENTER_CRITICAL(&mux_);
  const MqttSettings copy = mqttSettings_;
  portEXIT_CRITICAL(&mux_);
  return copy;
}
RotationSettings ConfigPortal::rotationSettings() {
  portENTER_CRITICAL(&mux_);
  const RotationSettings copy = rotationSettings_;
  portEXIT_CRITICAL(&mux_);
  return copy;
}
void ConfigPortal::publish(const DeviceStatus& status) {
  portENTER_CRITICAL(&mux_);
  status_ = status;
  portEXIT_CRITICAL(&mux_);
}
WeatherSettings ConfigPortal::weatherSettings() {
  portENTER_CRITICAL(&mux_); const auto copy = weatherSettings_; portEXIT_CRITICAL(&mux_); return copy;
}
ConfigPortal::SaveResult ConfigPortal::saveLocked(const Settings& next) {
  if (!storageReady_ || !validSettings(next)) return SaveResult::unavailable;
  const auto record = encodeSettings(next);
  // NVS commits one complete versioned blob. Publish only after success.
  if (storage_.putBytes("settings", record.data(), record.size()) != record.size()) return SaveResult::unavailable;
  portENTER_CRITICAL(&mux_);
  settings_ = next;
  portEXIT_CRITICAL(&mux_);
  return SaveResult::ok;
}
bool ConfigPortal::cycleBrightness() {
  if (!writer_ || xSemaphoreTake(writer_, 0) != pdTRUE) return false;
  Settings next = settings();
  next.brightness = next.brightness % 4 + 1;
  const auto result = saveLocked(next);
  xSemaphoreGive(writer_);
  return result == SaveResult::ok;
}

bool ConfigPortal::start() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192;
  config.task_priority = 1;
  config.max_open_sockets = 3;
  config.lru_purge_enable = true;
  config.recv_wait_timeout = 2;
  config.send_wait_timeout = 2;
  config.max_uri_handlers = 10;
  if (httpd_start(&server_, &config) != ESP_OK) return false;
  const char* paths[] = {"/", "/style.css", "/app.js", "/api/state", "/api/settings", "/api/messages/ack", "/api/mqtt", "/api/rotation", "/api/weather"};
  for (size_t i = 0; i < 9; ++i) {
    httpd_uri_t route{};
    route.uri = paths[i];
    route.method = i >= 4 ? HTTP_POST : HTTP_GET;
    route.handler = dispatch;
    route.user_ctx = this;
    if (httpd_register_uri_handler(server_, &route) != ESP_OK) {
      httpd_stop(server_); server_ = nullptr; return false;
    }
  }
  return true;
}
esp_err_t ConfigPortal::dispatch(httpd_req_t* request) {
  return static_cast<ConfigPortal*>(request->user_ctx)->handle(request);
}
esp_err_t ConfigPortal::send(httpd_req_t* request, const char* type, const char* body) {
  httpd_resp_set_type(request, type);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "Content-Security-Policy", "default-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
  return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
}
esp_err_t ConfigPortal::error(httpd_req_t* request, const char* status, const char* message) {
  httpd_resp_set_status(request, status);
  const std::string body = "{\"error\":" + jsonQuote(message) + "}";
  return send(request, "application/json; charset=utf-8", body.c_str());
}
bool ConfigPortal::authorized(httpd_req_t* request) {
  char header[sizeof(authorization_)]{};
  const size_t length = std::strlen(authorization_);
  if (httpd_req_get_hdr_value_len(request, "Authorization") == length &&
      httpd_req_get_hdr_value_str(request, "Authorization", header, sizeof(header)) == ESP_OK &&
      equalSecret(header, authorization_, length)) return true;
  httpd_resp_set_hdr(request, "WWW-Authenticate", "Basic realm=\"VFD Clock\", charset=\"UTF-8\"");
  error(request, "401 Unauthorized", "请使用 admin 和设备访问密钥登录。");
  return false;
}
bool ConfigPortal::csrfValid(httpd_req_t* request) {
  char token[sizeof(csrf_)]{};
  return httpd_req_get_hdr_value_len(request, "X-CSRF-Token") == 32 &&
         httpd_req_get_hdr_value_str(request, "X-CSRF-Token", token, sizeof(token)) == ESP_OK &&
         equalSecret(token, csrf_, 32);
}
std::string ConfigPortal::stateJson() {
  DeviceStatus status;
  Settings current;
  MqttSettings mqtt;
  RotationSettings rotation;
  WeatherSettings weather;
  portENTER_CRITICAL(&mux_);
  status = status_; current = settings_; mqtt = mqttSettings_; rotation = rotationSettings_; weather = weatherSettings_;
  portEXIT_CRITICAL(&mux_);
  char numbers[220];
  std::snprintf(numbers, sizeof(numbers),
    ",\"brightness\":%u,\"offset\":%d,\"connected\":%s,\"setupActive\":%s,\"timeValid\":%s,\"sensorEnabled\":%s,\"sampleFresh\":%s,\"uptime\":%lu",
    current.brightness, current.utcOffsetMinutes, status.connected ? "true" : "false", status.setupActive ? "true" : "false",
    status.timeValid ? "true" : "false", status.sensorEnabled ? "true" : "false", status.sampleFresh ? "true" : "false",
    static_cast<unsigned long>(status.uptimeSeconds));
  std::string result = "{\"ssid\":" + jsonQuote(current.ssid) + ",\"ntp\":" + jsonQuote(current.ntp) +
    ",\"clock\":" + jsonQuote(status.clock) + ",\"ip\":" + jsonQuote(status.ip) + ",\"csrf\":" + jsonQuote(csrf_) +
    ",\"passwordSet\":" + (current.password[0] ? "true" : "false") +
    ",\"storageReady\":" + (storageReady_ ? "true" : "false") + numbers;
  if (status.sampleValid) {
    char values[80];
    std::snprintf(values, sizeof(values), ",\"temperature\":%.1f,\"humidity\":%.1f", status.temperature, status.humidity);
    result += values;
  } else result += ",\"temperature\":null,\"humidity\":null";
  result += ",\"sensor\":{\"error\":" + std::to_string(status.sensorError) +
    ",\"samples\":" + std::to_string(status.sensorSamples) + ",\"failures\":" + std::to_string(status.sensorFailures) +
    ",\"consecutiveFailures\":" + std::to_string(status.sensorConsecutiveFailures) +
    ",\"recoveries\":" + std::to_string(status.sensorRecoveries) + "}";
  result += ",\"mqttState\":" + jsonQuote(status.mqttState) +
    ",\"messageId\":" + std::to_string(status.messageId) +
    ",\"messageCount\":" + std::to_string(status.messageCount) +
    ",\"messagesDropped\":" + std::to_string(status.messagesDropped) +
    ",\"messagesDuplicate\":" + std::to_string(status.messagesDuplicate) +
    ",\"messageVisible\":" + (status.messageVisible ? "true" : "false");
  result += ",\"mqtt\":" + mqttPublicJson(mqtt);
  result += ",\"rotation\":{\"pages\":" + std::to_string(rotation.pages) +
    ",\"hold\":" + std::to_string(rotation.holdSeconds) + "},\"contentPage\":" + jsonQuote(status.contentPage) +
    ",\"rotationPaused\":" + (status.rotationPaused ? "true" : "false");
  result += ",\"weather\":" + weatherPublicJson(weather);
  const auto& outdoor = status.outdoor;
  char values[160];
  std::snprintf(values, sizeof(values), ",\"temperature\":%.1f,\"humidity\":", outdoor.temperature);
  result += ",\"outdoor\":{\"state\":" + jsonQuote(status.weatherState) +
    ",\"httpStatus\":" + std::to_string(status.weatherHttpStatus) +
    ",\"fresh\":" + (outdoor.fresh(millis()) ? "true" : "false") +
    ",\"city\":" + jsonQuote(outdoor.city) + ",\"text\":" + jsonQuote(outdoor.text) +
    ",\"updated\":" + jsonQuote(outdoor.updated);
  if (outdoor.valid) result += values;
  else result += ",\"temperature\":null,\"humidity\":";
  if (outdoor.valid && outdoor.humidityValid) {
    std::snprintf(values, sizeof(values), "%.1f", outdoor.humidity); result += values;
  } else result += "null";
  result += "}";
  return result + "}";
}
esp_err_t ConfigPortal::handle(httpd_req_t* request) {
  if (!authorized(request)) return ESP_OK;
  // No query parameters are accepted: they must never become settings input.
  if (std::strchr(request->uri, '?')) return error(request, "400 Bad Request", "该接口不接受查询参数。");
  if (std::strcmp(request->uri, "/") == 0) return send(request, "text/html; charset=utf-8", web::index);
  if (std::strcmp(request->uri, "/style.css") == 0) return send(request, "text/css; charset=utf-8", web::style);
  if (std::strcmp(request->uri, "/app.js") == 0) return send(request, "application/javascript; charset=utf-8", web::app);
  if (std::strcmp(request->uri, "/api/state") == 0) {
    const auto state = stateJson();
    return send(request, "application/json; charset=utf-8", state.c_str());
  }
  if (std::strcmp(request->uri, "/api/messages/ack") == 0) return acknowledgeMessage(request);
  if (std::strcmp(request->uri, "/api/mqtt") == 0) return updateMqtt(request);
  if (std::strcmp(request->uri, "/api/rotation") == 0) return updateRotation(request);
  if (std::strcmp(request->uri, "/api/weather") == 0) return updateWeather(request);
  return update(request);
}
bool ConfigPortal::receiveForm(httpd_req_t* request, std::string& body, size_t maximum) {
  if (!csrfValid(request)) { error(request, "403 Forbidden", "页面会话已失效，请刷新后重试。"); return false; }
  if (request->content_len == 0 || request->content_len > maximum) { error(request, "413 Content Too Large", "请求大小超出限制。"); return false; }
  char type[64]{};
  if (httpd_req_get_hdr_value_str(request, "Content-Type", type, sizeof(type)) != ESP_OK ||
      std::strcmp(type, "application/x-www-form-urlencoded") != 0) {
    error(request, "415 Unsupported Media Type", "请使用表单提交。"); return false;
  }
  body.assign(request->content_len, '\0');
  size_t received = 0;
  const uint32_t startedAt = millis();
  while (received < body.size()) {
    if (static_cast<uint32_t>(millis() - startedAt) > 3000) {
      error(request, "408 Request Timeout", "接收超时，请重试。"); return false;
    }
    const int count = httpd_req_recv(request, &body[received], body.size() - received);
    if (count <= 0) { error(request, "408 Request Timeout", "接收失败，请重试。"); return false; }
    received += static_cast<size_t>(count);
  }
  return true;
}
bool ConfigPortal::takeMessageAck(uint32_t& id) {
  portENTER_CRITICAL(&mux_);
  id = pendingMessageAck_; pendingMessageAck_ = 0;
  portEXIT_CRITICAL(&mux_);
  return id != 0;
}
esp_err_t ConfigPortal::acknowledgeMessage(httpd_req_t* request) {
  std::string body;
  if (!receiveForm(request, body, 13)) return ESP_OK;
  uint32_t id = 0;
  if (!parseMessageAck(body, id)) return error(request, "400 Bad Request", "消息编号无效。");
  portENTER_CRITICAL(&mux_);
  const bool accepted = status_.messageVisible && status_.messageId == id &&
                        (!pendingMessageAck_ || pendingMessageAck_ == id);
  if (accepted) pendingMessageAck_ = id;
  portEXIT_CRITICAL(&mux_);
  if (!accepted) return error(request, "409 Conflict", "当前消息已变化或暂不可确认，请刷新状态。");
  httpd_resp_set_status(request, "202 Accepted");
  return send(request, "application/json; charset=utf-8", "{\"accepted\":true}");
}
esp_err_t ConfigPortal::updateRotation(httpd_req_t* request) {
  std::string body;
  if (!receiveForm(request, body, 64)) return ESP_OK;
  RotationSettings next;
  if (!parseRotationForm(body, next)) return error(request, "400 Bad Request", "至少选择一项内容，停留时间须为 5–120 秒；字段不能缺失或重复。");
  if (xSemaphoreTake(writer_, pdMS_TO_TICKS(1000)) != pdTRUE) return error(request, "503 Service Unavailable", "配置正忙，请重试。");
  bool saved = false;
  if (storageReady_) {
    const auto record = encodeRotation(next);
    saved = storage_.putBytes("rotation", record.data(), record.size()) == record.size();
    if (saved) {
      portENTER_CRITICAL(&mux_); rotationSettings_ = next; portEXIT_CRITICAL(&mux_);
    }
  }
  xSemaphoreGive(writer_);
  if (!saved) return error(request, "503 Service Unavailable", "保存失败，当前轮播配置未改变。");
  return send(request, "application/json; charset=utf-8", "{\"ok\":true}");
}
esp_err_t ConfigPortal::updateWeather(httpd_req_t* request) {
  std::string body;
  if (!receiveForm(request, body, 1024)) return ESP_OK;
  WeatherInput input;
  if (!parseWeatherForm(body, input)) return error(request, "400 Bad Request", "天气字段缺失、重复或编码错误。");
  if (xSemaphoreTake(writer_, pdMS_TO_TICKS(1000)) != pdTRUE) return error(request, "503 Service Unavailable", "配置正忙，请重试。");
  WeatherSettings next;
  const char* validation = applyWeather(weatherSettings(), input, next);
  bool saved = false;
  if (!validation && storageReady_) {
    const auto record = encodeWeather(next);
    saved = storage_.putBytes("weather", record.data(), record.size()) == record.size();
    if (saved) { portENTER_CRITICAL(&mux_); weatherSettings_ = next; portEXIT_CRITICAL(&mux_); }
  }
  xSemaphoreGive(writer_);
  if (validation) return error(request, "400 Bad Request", "请核对城市、公私钥操作和 5–60 分钟刷新间隔；启用时城市及两项密钥不能为空。");
  if (!saved) return error(request, "503 Service Unavailable", "保存失败，天气配置未改变。");
  return send(request, "application/json; charset=utf-8", "{\"ok\":true}");
}
esp_err_t ConfigPortal::updateMqtt(httpd_req_t* request) {
  std::string body;
  if (!receiveForm(request, body, 2048)) return ESP_OK;
  MqttSettingsInput input;
  if (!parseMqttForm(body, input)) return error(request, "400 Bad Request", "MQTT 字段缺失、重复或编码错误。");
  if (xSemaphoreTake(writer_, pdMS_TO_TICKS(1000)) != pdTRUE) return error(request, "503 Service Unavailable", "配置正忙，请重试。");
  MqttSettings next;
  const char* validation = applyMqttSettings(mqttSettings(), input, next);
  bool saved = false;
  if (!validation && storageReady_) {
    const auto record = encodeMqttSettings(next);
    saved = storage_.putBytes("mqtt", record.data(), record.size()) == record.size();
    if (saved) {
      portENTER_CRITICAL(&mux_); mqttSettings_ = next; portEXIT_CRITICAL(&mux_);
    }
  }
  xSemaphoreGive(writer_);
  if (validation) return error(request, "400 Bad Request", validation);
  if (!saved) return error(request, "503 Service Unavailable", "保存失败，当前 MQTT 配置未改变。");
  return send(request, "application/json; charset=utf-8", "{\"ok\":true}");
}
esp_err_t ConfigPortal::update(httpd_req_t* request) {
  std::string body;
  if (!receiveForm(request, body, 1024)) return ESP_OK;
  SettingsInput input;
  if (!parseSettingsForm(body, input)) return error(request, "400 Bad Request", "配置字段缺失、重复或编码错误。");
  if (xSemaphoreTake(writer_, pdMS_TO_TICKS(1000)) != pdTRUE) return error(request, "503 Service Unavailable", "配置正忙，请重试。");
  Settings next;
  const char* validation = applySettings(settings(), input, next);
  const auto result = validation ? SaveResult::unavailable : saveLocked(next);
  xSemaphoreGive(writer_);
  if (validation) return error(request, "400 Bad Request", validation);
  if (result != SaveResult::ok) return error(request, "503 Service Unavailable", "保存失败，当前配置未改变。");
  return send(request, "application/json; charset=utf-8", "{\"ok\":true}");
}
}
