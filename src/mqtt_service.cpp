#include "clock/mqtt_service.h"
#include "clock/retry.h"
#include "clock/settings.h"
#include <Arduino.h>
#include <MQTT.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <time.h>

namespace clockapp {
namespace {
class SecureClient : public NetworkClientSecure {
 public:
  int connect(const char* host, uint16_t port) override { return NetworkClientSecure::connect(host, port, 1500); }
  int connect(IPAddress ip, uint16_t port) override { return NetworkClientSecure::connect(ip, port, 1500); }
};
}
bool MqttService::begin(const MqttSettings& settings) {
  if (!validMqttSettings(settings)) { state_ = MqttState::invalid; return false; }
  configure(settings);
  incoming_ = xQueueCreate(4, sizeof(IncomingMessage));
  if (!incoming_) { state_ = MqttState::unavailable; return false; }
  if (xTaskCreate(task, "mqtt", 10240, this, 1, nullptr) != pdPASS) {
    vQueueDelete(incoming_); incoming_ = nullptr; state_ = MqttState::unavailable; return false;
  }
  return true;
}
void MqttService::configure(const MqttSettings& settings) {
  if (!validMqttSettings(settings)) return;
  portENTER_CRITICAL(&mux_);
  if (!generation_ || !sameMqtt(desired_, settings)) {
    desired_ = settings; ++generation_; if (!generation_) ++generation_;
  }
  portEXIT_CRITICAL(&mux_);
}
bool MqttService::receive(IncomingMessage& message) {
  // Drain at most the fixed queue capacity; discard deliveries from an old config.
  for (unsigned i = 0; i < 4 && incoming_ && xQueueReceive(incoming_, &message, 0) == pdTRUE; ++i) {
    portENTER_CRITICAL(&mux_); const auto current = generation_; portEXIT_CRITICAL(&mux_);
    if (message.generation == current) return true;
    ++dropped_;
  }
  return false;
}
void MqttService::task(void* self) { static_cast<MqttService*>(self)->run(); }
void MqttService::run() {
  NetworkClient plain;
  SecureClient secure;
  plain.setConnectionTimeout(1500);
  secure.setHandshakeTimeout(5);
  secure.useBuiltinCACertBundle(); // certificate validation stays enabled
  MQTTClient client(512);
  MqttSettings active;
  uint32_t activeGeneration = 0;
  client.setOptions(30, true, 1000);
  client.onMessageAdvanced([this, &active, &activeGeneration](MQTTClient*, char* topic, char* bytes, int length) {
    if (std::strcmp(topic, active.topic) != 0 || length < 1 || length > 256) { ++dropped_; return; }
    IncomingMessage message;
    message.length = static_cast<uint16_t>(length); message.generation = activeGeneration;
    std::memcpy(message.payload.data(), bytes, static_cast<size_t>(length));
    if (xQueueSend(incoming_, &message, 0) != pdTRUE) ++dropped_;
  });
  RetrySchedule retry;
  uint32_t protocolDrops = 0;
  for (;;) {
    if (pauseRequested_.load()) {
      if (client.connected()) client.disconnect();
      plain.stop(); secure.stop();
      retry.reset();
      state_ = MqttState::waitingWifi;
      paused_ = true;
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    paused_ = false;
    MqttSettings desired;
    uint32_t generation;
    portENTER_CRITICAL(&mux_); desired = desired_; generation = generation_; portEXIT_CRITICAL(&mux_);
    if (generation != activeGeneration) {
      if (client.connected()) client.disconnect();
      plain.stop(); secure.stop();
      active = desired; activeGeneration = generation; retry.reset();
      Client& transport = active.tls ? static_cast<Client&>(secure) : static_cast<Client&>(plain);
      client.begin(active.host, active.port, transport);
      // begin() resets the protocol engine, including its overflow policy.
      client.dropOverflow(true);
    }
    if (!active.enabled) {
      state_ = MqttState::disabled;
    } else if (WiFi.status() != WL_CONNECTED) {
      if (client.connected()) client.disconnect();
      state_ = MqttState::waitingWifi; retry.reset();
    } else if (active.tls && time(nullptr) < 1704067200) {
      state_ = MqttState::waitingTime; // no TLS until a credible wall clock exists
    } else if (!client.connected()) {
      if (retry.ready(millis())) {
        state_ = MqttState::connecting;
        if (client.connect(active.clientId, active.username, active.password) &&
            client.subscribe(active.topic, 1)) {
          retry.reset(); state_ = MqttState::online;
        } else {
          client.disconnect(); retry.failed(millis()); state_ = MqttState::retrying;
        }
      }
    } else if (!client.loop()) {
      retry.failed(millis()); state_ = MqttState::retrying;
    } else state_ = MqttState::online;
    const auto dropped = client.droppedMessages();
    if (dropped != protocolDrops) { dropped_.fetch_add(dropped - protocolDrops); protocolDrops = dropped; }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
}
