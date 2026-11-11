#include "clock/mqtt_service.h"
#include "clock/retry.h"
#include "clock/message_payload.h"
#include "clock/settings.h"
#include <Arduino.h>
#include <MQTT.h>
#if defined(ARDUINO_ARCH_ESP8266)
#include "clock/esp8266_tls.h"
#include <new>
#else
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#endif
#include <time.h>

namespace clockapp {
struct MqttService::Worker {
  MqttService& service;
#if defined(ARDUINO_ARCH_ESP8266)
  WiFiClient plain;
  BearSSL::WiFiClientSecure secure;
#else
  NetworkClient plain;
  NetworkClientSecure secure;
#endif
  MQTTClient client{1280};
  MqttSettings active;
  uint32_t activeGeneration = 0;
  RetrySchedule retry;
  uint32_t protocolDrops = 0;

  explicit Worker(MqttService& owner) : service(owner) {
#if defined(ARDUINO_ARCH_ESP8266)
    plain.setTimeout(1500);
    secure.setTimeout(4000);
    secure.setTrustAnchors(&serviceTrustAnchors());
#if defined(CLOCK_ESP8266_MQTT_CA_PEM)
    // Optional CA PEM for a private broker.
    static const BearSSL::X509List privateCa(CLOCK_ESP8266_MQTT_CA_PEM);
    secure.setTrustAnchors(&privateCa);
#endif
#else
    plain.setConnectionTimeout(1500);
    // Set the inherited connection timeout directly: the timed connect overload
    // calls virtual connect(), so overriding connect() with it recurses forever.
    secure.setConnectionTimeout(1500);
    secure.setHandshakeTimeout(5);
    secure.useBuiltinCACertBundle(); // certificate validation stays enabled
#endif
    client.setOptions(30, true, 1000);
    client.onMessageAdvanced([this](MQTTClient*, char* topic, char* bytes, int length) {
      if (std::strcmp(topic, active.topic) != 0 || length < 1 || length > 1024) { ++service.dropped_; return; }
      MessagePayload decoded;
      if (!decodeMessagePayload(std::string_view(bytes,size_t(length)),decoded)) { ++service.dropped_; return; }
      IncomingMessage message;
      message.length=uint16_t(decoded.text.size()); message.level=decoded.level; message.generation=activeGeneration;
      std::memcpy(message.payload.data(),decoded.text.data(),decoded.text.size());
      service.mux_.lock();
      const auto result=service.incoming_.push(message);
      service.mux_.unlock();
      if (result==PendingMessages::Push::rejected) ++service.dropped_;
      else if (result==PendingMessages::Push::evicted) ++service.evicted_;
      else if (result==PendingMessages::Push::duplicate) ++service.duplicates_;
    });
  }

  void tick() {
    if (service.pauseRequested_.load()) {
      if (client.connected()) client.disconnect();
      plain.stop(); secure.stop();
      retry.reset();
      service.state_ = MqttState::waitingWifi;
      service.paused_ = true;
      return;
    }
    service.paused_ = false;
    MqttSettings desired;
    uint32_t generation;
    service.mux_.lock(); desired = service.desired_; generation = service.generation_; service.mux_.unlock();
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
      service.state_ = MqttState::disabled;
    } else if (WiFi.status() != WL_CONNECTED) {
      if (client.connected()) client.disconnect();
      service.state_ = MqttState::waitingWifi; retry.reset();
    } else if (active.tls && time(nullptr) < 1704067200) {
      service.state_ = MqttState::waitingTime; // no TLS until a credible wall clock exists
    } else if (!client.connected()) {
      if (retry.ready(millis())) {
        service.state_ = MqttState::connecting;
        if (client.connect(active.clientId, active.username, active.password) &&
            client.subscribe(active.topic, 1)) {
          retry.reset(); service.state_ = MqttState::online;
        } else {
          client.disconnect(); retry.failed(millis()); service.state_ = MqttState::retrying;
        }
      }
    } else if (!client.loop()) {
      retry.failed(millis()); service.state_ = MqttState::retrying;
    } else service.state_ = MqttState::online;
    const auto dropped = client.droppedMessages();
    if (dropped != protocolDrops) { service.dropped_.fetch_add(dropped - protocolDrops); protocolDrops = dropped; }
  }
};

bool MqttService::begin(const MqttSettings& settings) {
  if (!validMqttSettings(settings)) { state_ = MqttState::invalid; return false; }
  configure(settings);
#if defined(ARDUINO_ARCH_ESP8266)
  worker_ = new (std::nothrow) Worker(*this);
  if (!worker_) { state_ = MqttState::unavailable; return false; }
#else
  if (xTaskCreate(task, "mqtt", 10240, this, 1, nullptr) != pdPASS) {
    state_ = MqttState::unavailable; return false;
  }
#endif
  started_ = true;
  return true;
}
void MqttService::configure(const MqttSettings& settings) {
  if (!validMqttSettings(settings)) return;
  mux_.lock();
  if (!generation_ || !sameMqtt(desired_, settings)) {
    desired_ = settings; ++generation_; if (!generation_) ++generation_;
  }
  mux_.unlock();
}
bool MqttService::receive(IncomingMessage& message) {
  // Drain at most the fixed queue capacity; discard deliveries from an old config.
  for (unsigned i=0;i<PendingMessages::capacity;++i) {
    mux_.lock();
    const bool available=incoming_.pop(message);
    const auto current=generation_;
    mux_.unlock();
    if (!available) return false;
    if (message.generation==current) return true;
    ++dropped_;
  }
  return false;
}
#if defined(ARDUINO_ARCH_ESP8266)
void MqttService::poll() {
  if (worker_) worker_->tick();
}
void MqttService::releaseTls() {
  if (!worker_ || !worker_->active.tls) return;
  if (worker_->client.connected()) worker_->client.disconnect();
  worker_->secure.stop();
}
#else
void MqttService::task(void* self) { static_cast<MqttService*>(self)->run(); }
void MqttService::run() {
  Worker worker(*this);
  for (;;) {
    worker.tick();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
#endif
}
