#include "clock/weather_service.h"
#include "clock/weather_response.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
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
  int available() override { return 0; } int read() override { return -1; } int peek() override { return -1; }
  void flush() override {}
  std::string text; bool rejected=false;
 private: uint32_t started_;
};
std::string signedUrl(const WeatherSettings& settings,int64_t epoch) {
  const auto input=weatherSignatureInput(uint64_t(epoch),settings.publicKey);
  uint8_t digest[20]{}; unsigned char encoded[40]{}; size_t length=0;
  const auto* info=mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  if (!info || mbedtls_md_hmac(info,reinterpret_cast<const uint8_t*>(settings.privateKey),std::strlen(settings.privateKey),
      reinterpret_cast<const uint8_t*>(input.data()),input.size(),digest)!=0 ||
      mbedtls_base64_encode(encoded,sizeof(encoded),&length,digest,sizeof(digest))!=0) return {};
  return "https://api.seniverse.com/v3/weather/now.json?"+input+"&sig="+
      weatherUrlEncode(std::string_view(reinterpret_cast<char*>(encoded),length))+"&location="+
      weatherUrlEncode(settings.location)+"&language=zh-Hans&unit=c";
}
WeatherSnapshot fetchWeather(const WeatherSettings& settings) {
  WeatherSnapshot result; result.state=WeatherState::retrying;
  const auto url=signedUrl(settings,int64_t(time(nullptr)));
  if (url.empty()) { result.state=WeatherState::unavailable; return result; }
  NetworkClientSecure secure; secure.useBuiltinCACertBundle(); secure.setHandshakeTimeout(4);
  HTTPClient http; http.setConnectTimeout(1500); http.setTimeout(4000);
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
  started_=xTaskCreate(task,"weather",12288,this,1,nullptr)==pdPASS;
  if (!started_) { portENTER_CRITICAL(&mux_); snapshot_.state=WeatherState::unavailable; portEXIT_CRITICAL(&mux_); }
  return started_;
}
void WeatherService::configure(const WeatherSettings& settings) {
  if (!validWeatherSettings(settings)) return;
  portENTER_CRITICAL(&mux_);
  if (!generation_ || !sameWeather(desired_,settings)) {
    desired_=settings; ++generation_; if (!generation_) ++generation_;
    snapshot_={}; snapshot_.state=settings.enabled?WeatherState::waitingWifi:WeatherState::disabled;
  }
  portEXIT_CRITICAL(&mux_);
}
WeatherSnapshot WeatherService::snapshot() {
  portENTER_CRITICAL(&mux_); const auto copy=snapshot_; portEXIT_CRITICAL(&mux_); return copy;
}
void WeatherService::task(void* self) { static_cast<WeatherService*>(self)->run(); }
void WeatherService::run() {
  uint32_t activeGeneration=0,lastAttempt=0,interval=0;
  bool scheduled=false,wasOnline=false;
  for (;;) {
    if (pauseRequested_.load()) { paused_=true; scheduled=false; vTaskDelay(pdMS_TO_TICKS(50)); continue; }
    paused_=false;
    WeatherSettings settings; uint32_t generation;
    portENTER_CRITICAL(&mux_); settings=desired_; generation=generation_; portEXIT_CRITICAL(&mux_);
    if (activeGeneration!=generation) { activeGeneration=generation; scheduled=false; }
    const bool online=WiFi.status()==WL_CONNECTED;
    if (online && !wasOnline) scheduled=false;
    wasOnline=online;
    const auto setState=[&](WeatherState state) {
      portENTER_CRITICAL(&mux_); if (generation_==generation) snapshot_.state=state; portEXIT_CRITICAL(&mux_);
    };
    if (!settings.enabled) setState(WeatherState::disabled);
    else if (!online) setState(WeatherState::waitingWifi);
    else if (time(nullptr)<1704067200) setState(WeatherState::waitingTime);
    else if (!scheduled || elapsed(millis(),lastAttempt,interval)) {
      setState(WeatherState::fetching);
      const auto result=fetchWeather(settings);
      portENTER_CRITICAL(&mux_);
      if (generation_==generation) {
        snapshot_.state=result.state; snapshot_.httpStatus=result.httpStatus;
        std::memcpy(snapshot_.errorCode,result.errorCode,sizeof(snapshot_.errorCode));
        if (result.observation.valid) snapshot_.observation=result.observation;
      }
      portEXIT_CRITICAL(&mux_);
      lastAttempt=millis(); scheduled=true;
      // Avoid rapid quota consumption, especially on bad credentials or rate limits.
      interval=result.state==WeatherState::online?uint32_t(settings.refreshMinutes)*60000:
          (result.state==WeatherState::authError || result.state==WeatherState::rateLimited)?900000:60000;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
}
