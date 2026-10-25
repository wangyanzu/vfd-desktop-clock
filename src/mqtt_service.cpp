#include "clock/mqtt_service.h"
#include "clock/retry.h"
#include "clock/message_payload.h"
#include "clock/settings.h"
#include <Arduino.h>
#include <MQTT.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <time.h>

namespace clockapp {
bool MqttService::begin(const MqttSettings& settings) {
  if (!validMqttSettings(settings)) { state_ = MqttState::invalid; return false; }
  configure(settings);
  if (xTaskCreate(task, "mqtt", 10240, this, 1, nullptr) != pdPASS) {
    state_ = MqttState::unavailable; return false;
  }
  started_ = true;
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
  for (unsigned i=0;i<PendingMessages::capacity;++i) {
    portENTER_CRITICAL(&mux_);
    const bool available=incoming_.pop(message);
    const auto current=generation_;
    portEXIT_CRITICAL(&mux_);
    if (!available) return false;
    if (message.generation==current) return true;
    ++dropped_;
  }
  return false;
}
void MqttService::task(void* self) { static_cast<MqttService*>(self)->run(); }
void MqttService::run() {
  NetworkClient plain;
  NetworkClientSecure secure;
  plain.setConnectionTimeout(1500);
  // Set the inherited connection timeout directly: the timed connect overload
  // calls virtual connect(), so overriding connect() with it recurses forever.
  secure.setConnectionTimeout(1500);
  secure.setHandshakeTimeout(5);
  secure.useBuiltinCACertBundle(); // certificate validation stays enabled
  MQTTClient client(1280);
  MqttSettings active;
  uint32_t activeGeneration = 0;
  client.setOptions(30, true, 1000);
  client.onMessageAdvanced([this, &active, &activeGeneration](MQTTClient*, char* topic, char* bytes, int length) {
    if (std::strcmp(topic, active.topic) != 0 || length < 1 || length > 1024) { ++dropped_; return; }
    MessagePayload decoded;
    if (!decodeMessagePayload(std::string_view(bytes,size_t(length)),decoded)) { ++dropped_; return; }
    IncomingMessage message;
    message.length=uint16_t(decoded.text.size()); message.level=decoded.level; message.generation=activeGeneration;
    std::memcpy(message.payload.data(),decoded.text.data(),decoded.text.size());
    portENTER_CRITICAL(&mux_);
    const auto result=incoming_.push(message);
    portEXIT_CRITICAL(&mux_);
    if (result==PendingMessages::Push::rejected) ++dropped_;
    else if (result==PendingMessages::Push::evicted) ++evicted_;
    else if (result==PendingMessages::Push::duplicate) ++duplicates_;
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
