#include "clock/weather_response.h"
#include <cassert>
#include <iostream>
using namespace clockapp;
int main() {
  int64_t now=0; assert(weatherEpoch("2026-10-25T12:00:00+08:00",now));
  const std::string prefix=R"({"results":[{"location":{"name":"Shanghai"},"now":{)";
  const std::string suffix=R"(},"last_update":"2026-10-25T11:50:00+08:00"}]})";
  const auto body=[&](const std::string& fields) { return prefix+fields+suffix; };
  const std::string base=R"("temperature":"18","code":"4","text":"Cloudy")";
  assert(weatherErrorCode(R"({"status":"The location can not be found.","status_code":"AP010010"})") == "AP010010");
  for (const auto& invalid : {R"({"status_code":"AP010010","status_code":"AP010005"})",R"({"status_code":"<script>"})",R"({"status_code":12})",R"({"status_code":"AP010010"}junk)"}) assert(weatherErrorCode(invalid).empty());
  OutdoorWeather sample;
  assert(parseWeatherResponse(body(base),now,100,sample));
  assert(sample.temperature==18 && !sample.humidityValid && sample.fresh(100) && sample.sourceAgeSeconds==600);
  assert(parseWeatherResponse(body(base+R"(,"humidity":"76")"),now,200,sample) && sample.humidityValid && sample.humidity==76);
  assert(parseWeatherResponse(body(base+R"(,"humidity":"unknown")"),now,200,sample) && !sample.humidityValid);
  const auto previous=sample;
  for (const auto& input : {std::string("{}"),std::string("{\"results\":[]}"),std::string("{\"status_code\":\"AP010001\"}"),
      body(R"("temperature":"N/A","code":"4","text":"Cloudy")"),body(base+R"(,"temperature":"20")"),body(base)+"junk",std::string(4097,'x')}) {
    assert(!parseWeatherResponse(input,now,300,sample));
    assert(sample.receivedAt==previous.receivedAt && sample.temperature==previous.temperature);
  }
  assert(!parseWeatherResponse(body(base),now-1000,100,sample)); // future observation
  assert(parseWeatherResponse(body(base),now+5400,100,sample) && !sample.fresh(100)); // old observations remain marked stale
  std::string nested(20,'['); nested += "0"; nested += std::string(20,']');
  assert(!parseWeatherResponse(nested,now,100,sample));
  std::cout << "PASS: real cJSON weather parsing, free/paid fields, duplicate/malformed/oversize responses, timezone/future/stale observations\n";
}
