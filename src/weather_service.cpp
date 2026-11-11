#include "clock/weather_service.h"
#include "clock/weather_response.h"
#include <Arduino.h>
#if defined(ARDUINO_ARCH_ESP8266)
#include "clock/esp8266_tls.h"
#include "clock/mqtt_service.h"
#include <ESP8266HTTPClient.h>
#include <base64.h>
#else
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#endif
#include <time.h>

namespace clockapp {
namespace {
class BoundedBody : public Stream {
 public:
  explicit BoundedBody(uint32_t started):started_(started) { text.reserve(1024); }
  size_t write(uint8_t value) override { return write(&value,1); }
  size_t write(const uint8_t* bytes,size_t count) override {
    if (text.size()+count>4096 || elapsed(millis(),started_,10000)) { rejected=true; return 0; }
    text.append(reinterpret_cast<const char*>(bytes),count); return count;
  }
#if defined(ARDUINO_ARCH_ESP8266)
  // HTTPClient uses Stream::sendSize(), which will not call write() when
  // availableForWrite() is zero. Keep chunks small and let write() enforce
  // the total limit (and report overflow even when exactly at the limit).
  int availableForWrite() override { return rejected ? 0 : 256; }
#endif
  int available() override { return 0; } int read() override { return -1; } int peek() override { return -1; }
  void flush() override {}
  std::string text; bool rejected=false;
 private: uint32_t started_;
};
std::string signedUrl(const WeatherSettings& settings,int64_t epoch) {
  const auto input=weatherSignatureInput(uint64_t(epoch),settings.publicKey);
  uint8_t digest[20]{};
#if defined(ARDUINO_ARCH_ESP8266)
  br_hmac_key_context key;
  br_hmac_context hmac;
  br_hmac_key_init(&key, &br_sha1_vtable, settings.privateKey, std::strlen(settings.privateKey));
  br_hmac_init(&hmac, &key, sizeof(digest));
  br_hmac_update(&hmac, input.data(), input.size());
  br_hmac_out(&hmac, digest);
  const auto signature = base64::encode(digest, sizeof(digest));
  if (signature.isEmpty()) return {};
  const auto encodedSignature = std::string_view(signature.c_str(), signature.length());
#else
  unsigned char encoded[40]{}; size_t length=0;
  const auto* info=mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  if (!info || mbedtls_md_hmac(info,reinterpret_cast<const uint8_t*>(settings.privateKey),std::strlen(settings.privateKey),
      reinterpret_cast<const uint8_t*>(input.data()),input.size(),digest)!=0 ||
      mbedtls_base64_encode(encoded,sizeof(encoded),&length,digest,sizeof(digest))!=0) return {};
  const auto encodedSignature = std::string_view(reinterpret_cast<char*>(encoded),length);
#endif
  return "https://api.seniverse.com/v3/weather/now.json?"+input+"&sig="+
      weatherUrlEncode(encodedSignature)+"&location="+
      weatherUrlEncode(settings.location)+"&language=zh-Hans&unit=c";
}
WeatherSnapshot fetchWeather(const WeatherSettings& settings) {
  WeatherSnapshot result; result.state=WeatherState::retrying;
  const auto url=signedUrl(settings,int64_t(time(nullptr)));
  if (url.empty()) { result.state=WeatherState::unavailable; return result; }
#if defined(ARDUINO_ARCH_ESP8266)
  BearSSL::WiFiClientSecure secure;
  secure.setTrustAnchors(&serviceTrustAnchors());
  secure.setTimeout(4000);
  HTTPClient http;
  http.setTimeout(4000);
#else
  NetworkClientSecure secure; secure.useBuiltinCACertBundle(); secure.setHandshakeTimeout(4);
  HTTPClient http; http.setConnectTimeout(1500); http.setTimeout(4000);
#endif
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(secure,url.c_str())) return result;
  const auto started=millis(); result.httpStatus=http.GET();
  if (result.httpStatus>0) {
    BoundedBody body(started);
    const bool received=http.getSize()<=4096 && http.writeToStream(&body)>=0 && !body.rejected;
    if (received) {
      const auto code=weatherErrorCode(body.text);
      std::snprintf(result.errorCode,sizeof(result.errorCode),"%s",code.c_str());
    }
    if (result.httpStatus==401 || result.httpStatus==403) result.state=WeatherState::authError;
    else if (result.httpStatus==429) result.state=WeatherState::rateLimited;
    else if (result.httpStatus==200) {
      if (received && parseWeatherResponse(body.text,int64_t(time(nullptr)),millis(),result.observation)) result.state=WeatherState::online;
      else result.state=WeatherState::invalidResponse;
    }
    if (std::strcmp(result.errorCode,"AP010014")==0) result.state=WeatherState::rateLimited;
  }
  http.end(); secure.stop(); return result;
}
}
bool WeatherService::begin(const WeatherSettings& settings) {
  if (!validWeatherSettings(settings)) return false;
  configure(settings);
#if defined(ARDUINO_ARCH_ESP8266)
  started_=true;
#else
  started_=xTaskCreate(task,"weather",12288,this,1,nullptr)==pdPASS;
  if (!started_) { mux_.lock(); snapshot_.state=WeatherState::unavailable; mux_.unlock(); }
#endif
  return started_;
}
void WeatherService::configure(const WeatherSettings& settings) {
  if (!validWeatherSettings(settings)) return;
  mux_.lock();
  if (!generation_ || !sameWeather(desired_,settings)) {
    desired_=settings; ++generation_; if (!generation_) ++generation_;
    snapshot_={}; snapshot_.state=settings.enabled?WeatherState::waitingWifi:WeatherState::disabled;
  }
  mux_.unlock();
}
WeatherSnapshot WeatherService::snapshot() {
  mux_.lock(); const auto copy=snapshot_; mux_.unlock(); return copy;
}
#if defined(ARDUINO_ARCH_ESP8266)
void WeatherService::poll(MqttService& mqtt) {
  if (started_) tick(&mqtt);
}
#else
void WeatherService::task(void* self) { static_cast<WeatherService*>(self)->run(); }
void WeatherService::run() {
  for (;;) {
    tick(nullptr);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
#endif
void WeatherService::tick([[maybe_unused]] MqttService* mqtt) {
  if (pauseRequested_.load()) { paused_=true; scheduled_=false; return; }
  paused_=false;
  WeatherSettings settings; uint32_t generation;
  mux_.lock(); settings=desired_; generation=generation_; mux_.unlock();
  if (activeGeneration_!=generation) { activeGeneration_=generation; scheduled_=false; }
  const bool online=WiFi.status()==WL_CONNECTED;
  if (online && !wasOnline_) scheduled_=false;
  wasOnline_=online;
  const auto setState=[&](WeatherState state) {
    mux_.lock(); if (generation_==generation) snapshot_.state=state; mux_.unlock();
  };
  if (!settings.enabled) setState(WeatherState::disabled);
  else if (!online) setState(WeatherState::waitingWifi);
  else if (time(nullptr)<1704067200) setState(WeatherState::waitingTime);
  else if (!scheduled_ || elapsed(millis(),lastAttempt_,interval_)) {
    setState(WeatherState::fetching);
#if defined(ARDUINO_ARCH_ESP8266)
    // Two simultaneous TLS connections exceed the available heap on this MCU.
    mqtt->releaseTls();
#endif
    const auto result=fetchWeather(settings);
    mux_.lock();
    if (generation_==generation) {
      snapshot_.state=result.state; snapshot_.httpStatus=result.httpStatus;
      std::memcpy(snapshot_.errorCode,result.errorCode,sizeof(snapshot_.errorCode));
      if (result.observation.valid) snapshot_.observation=result.observation;
    }
    mux_.unlock();
    lastAttempt_=millis(); scheduled_=true;
    // Avoid rapid quota consumption, especially on bad credentials or rate limits.
    interval_=result.state==WeatherState::online?uint32_t(settings.refreshMinutes)*60000:
        (result.state==WeatherState::authError || result.state==WeatherState::rateLimited)?900000:60000;
  }
}
}
