#include "clock/portal.h"
#include "clock/messages.h"
#include "clock/web_assets.h"
#include <Arduino.h>
#if defined(ARDUINO_ARCH_ESP8266)
#include <base64.h>
extern "C" {
#include <osapi.h>
}
#else
#include "w4home_client.h"
#include "w4home/idf_portal.h"
#include <esp_random.h>
#include <mbedtls/base64.h>
#endif
#include <algorithm>

namespace clockapp {
#if !defined(ARDUINO_ARCH_ESP8266)
static ConfigPortal *localPortal;
#endif
namespace {
void fillRandom(void* output, size_t size) {
#if defined(ARDUINO_ARCH_ESP8266)
  os_get_random(static_cast<unsigned char*>(output), size);
#else
  esp_fill_random(output, size);
#endif
}
constexpr PortalResult requestHandled = 0;
void responseStatus(PortalRequest* request, const char* status) {
#if defined(ARDUINO_ARCH_ESP8266)
  request->status = std::atoi(status);
#else
  httpd_resp_set_status(request, status);
#endif
}
void responseHeader(PortalRequest* request, const char* name, const char* value) {
#if defined(ARDUINO_ARCH_ESP8266)
  request->server.sendHeader(name, value);
#else
  httpd_resp_set_hdr(request, name, value);
#endif
}
bool requestHeader(PortalRequest* request, const char* name, char* output, size_t capacity) {
#if defined(ARDUINO_ARCH_ESP8266)
  const auto value = request->server.header(name);
  if (value.isEmpty() || value.length() >= capacity) return false;
  std::memcpy(output, value.c_str(), value.length() + 1);
  return true;
#else
  return httpd_req_get_hdr_value_str(request, name, output, capacity) == ESP_OK;
#endif
}
void randomHex(char* output, size_t length) {
  const char hex[] = "0123456789ABCDEF";
  uint8_t bytes[16];
  fillRandom(bytes, sizeof(bytes));
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
    do { fillRandom(&value, sizeof(value)); } while (value >= 250);
    output[i] = '0' + value % 10;
  }
  output[8] = 0;
}
}

bool ConfigPortal::begin(const Settings& defaults, const MqttSettings& mqttDefaults) {
  settings_ = defaults; mqttSettings_ = mqttDefaults;
  if (!writer_.begin()) return false;
#if defined(ARDUINO_ARCH_ESP8266)
  storageReady_ = storage_.begin();
#else
  storageReady_ = storage_.begin("clock", false);
#endif
  if (storageReady_) {
    SettingsRecord record{};
    if (storage_.getBytesLength("settings") == record.size() &&
        storage_.getBytes("settings", record.data(), record.size()) == record.size()) {
      if (!decodeSettings(record, settings_)) Serial.println("Invalid settings record; using defaults.");
    } else if (storage_.getBytesLength("settings") == LegacySettingsRecord{}.size()) {
      LegacySettingsRecord previous{};
      if (storage_.getBytes("settings",previous.data(),previous.size()) != previous.size() ||
          !decodeSettings(previous,settings_)) Serial.println("Invalid legacy settings; using defaults.");
    }
#if !defined(ARDUINO_ARCH_ESP8266)
    else {
      // Migrate brightness from the first firmware. Network remains in defaults.
      const auto previous = storage_.getUChar("brightness", defaults.brightness);
      if (previous >= 1 && previous <= 4) settings_.brightness = previous;
    }
#endif
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
#if !defined(ARDUINO_ARCH_ESP8266)
  SettingsRecord trial{};
  if(storage_.getBytesLength("localTrial")==trial.size()&&storage_.getBytes("localTrial",trial.data(),trial.size())==trial.size()){
    Settings staged{};if(decodeSettings(trial,staged)){std::snprintf(settings_.ssid,sizeof(settings_.ssid),"%s",staged.ssid);std::snprintf(settings_.password,sizeof(settings_.password),"%s",staged.password);}
  }
#endif
  randomHex(csrf_, 32);
  const std::string credentials = std::string("admin:") + accessKey_;
#if defined(ARDUINO_ARCH_ESP8266)
  const auto encoded = base64::encode(reinterpret_cast<const uint8_t*>(credentials.data()), credentials.size());
  if (encoded.isEmpty()) return false;
  std::snprintf(authorization_, sizeof(authorization_), "Basic %s", encoded.c_str());
#else
  unsigned char encoded[64]{};
  size_t length = 0;
  if (mbedtls_base64_encode(encoded, sizeof(encoded), &length,
                          reinterpret_cast<const unsigned char*>(credentials.data()), credentials.size()) != 0) return false;
  std::snprintf(authorization_, sizeof(authorization_), "Basic %s", encoded);
#endif
  return true;
}

Settings ConfigPortal::settings() {
  mux_.lock();
  const Settings copy = settings_;
  mux_.unlock();
  return copy;
}
MqttSettings ConfigPortal::mqttSettings() {
  mux_.lock();
  const MqttSettings copy = mqttSettings_;
  mux_.unlock();
  return copy;
}
RotationSettings ConfigPortal::rotationSettings() {
  mux_.lock();
  const RotationSettings copy = rotationSettings_;
  mux_.unlock();
  return copy;
}
void ConfigPortal::publish(const DeviceStatus& status) {
  mux_.lock();
  status_ = status;
  mux_.unlock();
}
WeatherSettings ConfigPortal::weatherSettings() {
  mux_.lock(); const auto copy = weatherSettings_; mux_.unlock(); return copy;
}
ConfigPortal::SaveResult ConfigPortal::saveLocked(const Settings& next) {
  if (!storageReady_ || !validSettings(next)) return SaveResult::unavailable;
  Settings stable=next;
#if !defined(ARDUINO_ARCH_ESP8266)
  if(storage_.isKey("localTrial")){
    SettingsRecord saved{};Settings original{};
    if(storage_.getBytes("localBackup",saved.data(),saved.size())!=saved.size()||!decodeSettings(saved,original))return SaveResult::unavailable;
    std::snprintf(stable.ssid,sizeof(stable.ssid),"%s",original.ssid);std::snprintf(stable.password,sizeof(stable.password),"%s",original.password);
  }
#endif
  const auto record = encodeSettings(stable);
  // NVS commits one complete versioned blob. Publish only after success.
  if (storage_.putBytes("settings", record.data(), record.size()) != record.size()) return SaveResult::unavailable;
  mux_.lock();
  settings_ = next;
  mux_.unlock();
  return SaveResult::ok;
}
bool ConfigPortal::cycleBrightness() {
  if (!writer_.take()) return false;
  Settings next = settings();
  next.brightness = next.brightness % 4 + 1;
  const auto result = saveLocked(next);
  writer_.give();
  return result == SaveResult::ok;
}

bool ConfigPortal::start() {
  const char* paths[] = {"/", "/style.css", "/app.js", "/api/state", "/api/settings", "/api/messages/ack", "/api/mqtt", "/api/rotation", "/api/weather"};
#if defined(ARDUINO_ARCH_ESP8266)
  server_.collectHeaders("Content-Type", "X-CSRF-Token"); // Authorization is always collected.
  for (size_t i = 0; i < sizeof(paths)/sizeof(paths[0]); ++i) {
    server_.on(paths[i], i >= 4 ? HTTP_POST : HTTP_GET, [this]() {
      server_.keepAlive(false);
      PortalRequest request{server_, server_.uri().c_str()};
      handle(&request);
    });
  }
  server_.begin();
  return true;
#else
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192;
  config.task_priority = 1;
  config.max_open_sockets = 3;
  config.lru_purge_enable = true;
  config.recv_wait_timeout = 2;
  config.send_wait_timeout = 2;
  config.max_uri_handlers = 18;
  if (httpd_start(&server_, &config) != ESP_OK) return false;
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
  localPortal=this;
  const w4local_ops_t ops={
   [](){localPortal->mux_.lock();bool active=localPortal->status_.setupActive;localPortal->mux_.unlock();return active;},
   [](const char *ssid,const char *password,const char *origin){return localPortal->localStage(ssid,password,origin);},
   [](){return localPortal->localCancel();},[](){return localPortal->localClear();},
   [](cJSON *j){char origin[192];bool ready=false,recovery=false;w4home_local_state(origin,sizeof(origin),&ready,&recovery);localPortal->mux_.lock();bool connected=localPortal->status_.connected;localPortal->mux_.unlock();cJSON_AddBoolToObject(j,"configured",ready);cJSON_AddBoolToObject(j,"wifiConnected",connected);cJSON_AddBoolToObject(j,"authRecoveryRequired",recovery);cJSON_AddBoolToObject(j,"pendingSetup",localPortal->storage_.isKey("localTrial"));cJSON_AddBoolToObject(j,"serverConfigured",true);cJSON_AddStringToObject(j,"server",origin);},
   []()->const char*{static char origin[192];bool ready=false,recovery=false;w4home_local_state(origin,sizeof(origin),&ready,&recovery);return origin;}
  };
  return w4local_register(server_,&ops)==ESP_OK;
#endif
}
#if !defined(ARDUINO_ARCH_ESP8266)
PortalResult ConfigPortal::dispatch(PortalRequest* request) {
  return static_cast<ConfigPortal*>(request->user_ctx)->handle(request);
}
#endif
PortalResult ConfigPortal::send(PortalRequest* request, const char* type, const char* body) {
  responseHeader(request, "Cache-Control", "no-store");
  responseHeader(request, "X-Content-Type-Options", "nosniff");
  responseHeader(request, "Content-Security-Policy", "default-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
#if defined(ARDUINO_ARCH_ESP8266)
  if (body == web::index || body == web::style || body == web::app) {
    request->server.send_P(request->status, type, body, strlen_P(body));
  } else {
    request->server.send(request->status, type, body);
  }
  return requestHandled;
#else
  httpd_resp_set_type(request, type);
  return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
#endif
}
PortalResult ConfigPortal::error(PortalRequest* request, const char* status, const char* message) {
  responseStatus(request, status);
  const std::string body = "{\"error\":" + jsonQuote(message) + "}";
  return send(request, "application/json; charset=utf-8", body.c_str());
}
bool ConfigPortal::authorized(PortalRequest* request) {
  char header[sizeof(authorization_)]{};
  const size_t length = std::strlen(authorization_);
  if (requestHeader(request, "Authorization", header, sizeof(header)) && std::strlen(header) == length &&
      equalSecret(header, authorization_, length)) return true;
  responseHeader(request, "WWW-Authenticate", "Basic realm=\"VFD Clock\", charset=\"UTF-8\"");
  error(request, "401 Unauthorized", "请使用 admin 和设备访问密钥登录。");
  return false;
}
bool ConfigPortal::csrfValid(PortalRequest* request) {
  char token[sizeof(csrf_)]{};
  return requestHeader(request, "X-CSRF-Token", token, sizeof(token)) && std::strlen(token) == 32 &&
         equalSecret(token, csrf_, 32);
}
std::string ConfigPortal::stateJson() {
  DeviceStatus status;
  Settings current;
  MqttSettings mqtt;
  RotationSettings rotation;
  WeatherSettings weather;
  mux_.lock();
  status = status_; current = settings_; mqtt = mqttSettings_; rotation = rotationSettings_; weather = weatherSettings_;
  mux_.unlock();

  char numbers[220];
  std::snprintf(numbers, sizeof(numbers),
    ",\"brightness\":%u,\"offset\":%d,\"connected\":%s,\"setupActive\":%s,\"timeValid\":%s,\"sensorEnabled\":%s,\"sampleFresh\":%s,\"uptime\":%lu",
    current.brightness, current.utcOffsetMinutes, status.connected ? "true" : "false", status.setupActive ? "true" : "false",
    status.timeValid ? "true" : "false", status.sensorEnabled ? "true" : "false", status.sampleFresh ? "true" : "false",
    static_cast<unsigned long>(status.uptimeSeconds));
  std::string result = "{\"ssid\":" + jsonQuote(current.ssid) + ",\"ntp\":" + jsonQuote(current.ntp) +
    ",\"clock\":" + jsonQuote(status.clock) + ",\"ip\":" + jsonQuote(status.ip) + ",\"csrf\":" + jsonQuote(csrf_) +
    ",\"passwordSet\":" + (current.password[0] ? "true" : "false") +
    ",\"storageReady\":" + (storageReady_ ? "true" : "false") + numbers + ",\"sleepMinutes\":" + std::to_string(current.sleepMinutes);
#if defined(ARDUINO_ARCH_ESP8266)
  result += ",\"w4home\":null";
#else
  char center[160];
  w4home_diagnostics(center, sizeof(center));
  result += ",\"w4home\":" + std::string(center);
#endif
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
    ",\"messageLevel\":" + std::to_string(status.messageLevel) +
    ",\"messagesDropped\":" + std::to_string(status.messagesDropped) +
    ",\"messagesDuplicate\":" + std::to_string(status.messagesDuplicate) +
    ",\"messagesEvicted\":" + std::to_string(status.messagesEvicted) +
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
    ",\"errorCode\":" + jsonQuote(status.weatherErrorCode) +
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
PortalResult ConfigPortal::handle(PortalRequest* request) {
#if !defined(ARDUINO_ARCH_ESP8266)
  if(std::strcmp(request->uri,"/")==0&&w4local_on_ap(request))return w4local_page(request);
#endif
  if (!authorized(request)) return requestHandled;
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
bool ConfigPortal::receiveForm(PortalRequest* request, std::string& body, size_t maximum) {
  if (!csrfValid(request)) { error(request, "403 Forbidden", "页面会话已失效，请刷新后重试。"); return false; }

  char type[64]{};
  if (!requestHeader(request, "Content-Type", type, sizeof(type)) ||
      std::strcmp(type, "application/x-www-form-urlencoded") != 0) {
    error(request, "415 Unsupported Media Type", "请使用表单提交。"); return false;
  }
#if defined(ARDUINO_ARCH_ESP8266)
  // The core appends the raw body after the decoded arguments. Use the index
  // so a submitted field named "plain" cannot shadow it.
  const auto& raw = request->server.arg(request->server.args());
  const size_t size = raw.length();
#else
  const size_t size = request->content_len;
#endif
  if (!size || size > maximum) { error(request, "413 Content Too Large", "请求大小超出限制。"); return false; }
#if defined(ARDUINO_ARCH_ESP8266)
  body.assign(raw.c_str(), size);
#else
  body.assign(size, '\0');
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
#endif
  return true;
}
bool ConfigPortal::takeMessageAck(uint32_t& id) {
  mux_.lock();
  id = pendingMessageAck_; pendingMessageAck_ = 0;
  mux_.unlock();
  return id != 0;
}
PortalResult ConfigPortal::acknowledgeMessage(PortalRequest* request) {
  std::string body;
  if (!receiveForm(request, body, 13)) return requestHandled;
  uint32_t id = 0;
  if (!parseMessageAck(body, id)) return error(request, "400 Bad Request", "消息编号无效。");
  mux_.lock();
  const bool accepted = status_.messageVisible && status_.messageId == id &&
                        (!pendingMessageAck_ || pendingMessageAck_ == id);
  if (accepted) pendingMessageAck_ = id;
  mux_.unlock();
  if (!accepted) return error(request, "409 Conflict", "当前消息已变化或暂不可确认，请刷新状态。");
  responseStatus(request, "202 Accepted");
  return send(request, "application/json; charset=utf-8", "{\"accepted\":true}");
}
PortalResult ConfigPortal::updateRotation(PortalRequest* request) {
  std::string body;
  if (!receiveForm(request, body, 64)) return requestHandled;
  RotationSettings next;
  if (!parseRotationForm(body, next)) return error(request, "400 Bad Request", "至少选择一项内容，停留时间须为 5–120 秒；字段不能缺失或重复。");
  if (!writer_.take(1000)) return error(request, "503 Service Unavailable", "配置正忙，请重试。");
  bool saved = false;
  if (storageReady_) {
    const auto record = encodeRotation(next);
    saved = storage_.putBytes("rotation", record.data(), record.size()) == record.size();
    if (saved) {
      mux_.lock(); rotationSettings_ = next; mux_.unlock();
    }
  }
  writer_.give();
  if (!saved) return error(request, "503 Service Unavailable", "保存失败，当前轮播配置未改变。");
  return send(request, "application/json; charset=utf-8", "{\"ok\":true}");
}
PortalResult ConfigPortal::updateWeather(PortalRequest* request) {
  std::string body;
  if (!receiveForm(request, body, 1024)) return requestHandled;
  WeatherInput input;
  if (!parseWeatherForm(body, input)) return error(request, "400 Bad Request", "天气字段缺失、重复或编码错误。");
  if (!writer_.take(1000)) return error(request, "503 Service Unavailable", "配置正忙，请重试。");
  WeatherSettings next;
  const char* validation = applyWeather(weatherSettings(), input, next);
  bool saved = false;
  if (!validation && storageReady_) {
    const auto record = encodeWeather(next);
    saved = storage_.putBytes("weather", record.data(), record.size()) == record.size();
    if (saved) { mux_.lock(); weatherSettings_ = next; mux_.unlock(); }
  }
  writer_.give();
  if (validation) return error(request, "400 Bad Request", "请核对城市、公私钥操作和 5–60 分钟刷新间隔；启用时城市及两项密钥不能为空。");
  if (!saved) return error(request, "503 Service Unavailable", "保存失败，天气配置未改变。");
  return send(request, "application/json; charset=utf-8", "{\"ok\":true}");
}
PortalResult ConfigPortal::updateMqtt(PortalRequest* request) {
  std::string body;
  if (!receiveForm(request, body, 2048)) return requestHandled;
  MqttSettingsInput input;
  if (!parseMqttForm(body, input)) return error(request, "400 Bad Request", "MQTT 字段缺失、重复或编码错误。");
  if (!writer_.take(1000)) return error(request, "503 Service Unavailable", "配置正忙，请重试。");
  MqttSettings next;
  const char* validation = applyMqttSettings(mqttSettings(), input, next);
  bool saved = false;
  if (!validation && storageReady_) {
    const auto record = encodeMqttSettings(next);
    saved = storage_.putBytes("mqtt", record.data(), record.size()) == record.size();
    if (saved) {
      mux_.lock(); mqttSettings_ = next; mux_.unlock();
    }
  }
  writer_.give();
  if (validation) return error(request, "400 Bad Request", validation);
  if (!saved) return error(request, "503 Service Unavailable", "保存失败，当前 MQTT 配置未改变。");
  return send(request, "application/json; charset=utf-8", "{\"ok\":true}");
}
PortalResult ConfigPortal::update(PortalRequest* request) {
  std::string body;
  if (!receiveForm(request, body, 1024)) return requestHandled;
  SettingsInput input;
  if (!parseSettingsForm(body, input)) return error(request, "400 Bad Request", "配置字段缺失、重复或编码错误。");
  if (!writer_.take(1000)) return error(request, "503 Service Unavailable", "配置正忙，请重试。");
  Settings next;
  const char* validation = applySettings(settings(), input, next);
#if defined(ARDUINO_ARCH_ESP8266)
  const bool canChangeWifi = status_.setupActive && request->server.client().localIP() == WiFi.softAPIP();
#else
  constexpr bool canChangeWifi = false;
#endif
  if(!validation&&!canChangeWifi&&!sameNetwork(next,settings())){writer_.give();return error(request,"403 Forbidden","网络只在设备热点配网页中维护。");}
  const auto result = validation ? SaveResult::unavailable : saveLocked(next);
  writer_.give();
  if (validation) return error(request, "400 Bad Request", validation);
  if (result != SaveResult::ok) return error(request, "503 Service Unavailable", "保存失败，当前配置未改变。");
  return send(request, "application/json; charset=utf-8", "{\"ok\":true}");
}
#if !defined(ARDUINO_ARCH_ESP8266)
bool ConfigPortal::localStage(const char *ssid,const char *password,const char *origin){
 if(storage_.isKey("localTrial")||!w4home_idle()||!w4home_wifi_valid(ssid,password)||!writer_.take(1000))return false;
 Settings next=settings();bool ok=true;
 if(!storage_.isKey("localTrial")){auto backup=encodeSettings(next);ok=storage_.putBytes("localBackup",backup.data(),backup.size())==backup.size();}
 std::snprintf(next.ssid,sizeof(next.ssid),"%s",ssid);std::snprintf(next.password,sizeof(next.password),"%s",password);auto trial=encodeSettings(next);
 if(ok)ok=storage_.putBytes("localTrial",trial.data(),trial.size())==trial.size()&&storage_.putString("localOrigin",origin)==std::strlen(origin);
 if(ok)ok=w4home_service_request(origin,false);
 if(ok){mux_.lock();settings_=next;mux_.unlock();}
 else {storage_.remove("localTrial");storage_.remove("localBackup");}
 writer_.give();return ok;
}
bool ConfigPortal::localCancel(){
 if(!writer_.take(1000))return false;
 bool ok=w4home_service_request(nullptr,true);
 if(ok&&storage_.isKey("localTrial")){
  SettingsRecord raw{};Settings old{};ok=storage_.getBytes("localBackup",raw.data(),raw.size())==raw.size()&&decodeSettings(raw,old);
  if(ok){auto next=settings();std::snprintf(next.ssid,sizeof(next.ssid),"%s",old.ssid);std::snprintf(next.password,sizeof(next.password),"%s",old.password);ok=saveLocked(next)==SaveResult::ok;}
  if(ok)ok=storage_.remove("localTrial")&&storage_.remove("localBackup");
 }
 writer_.give();return ok;
}
bool ConfigPortal::localClear(){
 if(!localCancel()||!writer_.take(1000))return false;
 auto next=settings();next.ssid[0]=next.password[0]=0;bool ok=saveLocked(next)==SaveResult::ok;writer_.give();return ok;
}
void ConfigPortal::localTick(bool connected){
 if(!connected||!storage_.isKey("localTrial")||!writer_.take())return;
 char origin[192];bool ready=false,recovery=false;w4home_local_state(origin,sizeof(origin),&ready,&recovery);
 if(ready&&storage_.getString("localOrigin","")==origin){auto record=encodeSettings(settings());if(storage_.putBytes("settings",record.data(),record.size())==record.size()){storage_.remove("localTrial");storage_.remove("localBackup");}}
 writer_.give();
}

#endif
}
