#include "clock/weather_settings.h"
#include "clock/weather.h"
#include "clock/rotation.h"
#include <cassert>
#include <iostream>
using namespace clockapp;
int main() {
  WeatherSettings defaults,settings,decoded;
  assert(validWeatherSettings(defaults));
  WeatherInput in{"1","shanghai","15","public-fixture","private_fixture","replace","replace"};
  assert(!applyWeather(defaults,in,settings));
  auto record=encodeWeather(settings);
  assert(decodeWeather(record,decoded) && sameWeather(decoded,settings));
  for (size_t i=0;i<record.size();++i) {
    auto corrupt=record; corrupt[i]^=1;
    assert(!decodeWeather(corrupt,decoded) && sameWeather(decoded,settings));
  }
  auto bad=settings; std::memset(bad.privateKey,'x',65); assert(!validWeatherSettings(bad));
  const auto publicJson=weatherPublicJson(settings);
  assert(publicJson.find("public-fixture")==std::string::npos && publicJson.find("private_fixture")==std::string::npos);
  in.publicMode=in.privateMode="keep"; in.publicKey=in.privateKey="";
  in.location="beijing"; assert(!applyWeather(settings,in,decoded));
  assert(std::strcmp(decoded.privateKey,"private_fixture")==0);
  in.privateMode="clear"; assert(applyWeather(settings,in,decoded)); // enabled requires both
  assert(std::strcmp(decoded.privateKey,"private_fixture")==0); // transactional failure
  in.enabled="0"; assert(!applyWeather(settings,in,decoded) && !decoded.privateKey[0]);
  WeatherInput parsed;
  const std::string form="enabled=1&location=beijing&refresh=15&public_key=abc&private_key=def&public_mode=replace&private_mode=replace";
  assert(parseWeatherForm(form,parsed));
  for (const auto& body : {form+"&enabled=1",form+"&extra=1",form+"&",std::string("enabled=1"),std::string("enabled=%00")}) assert(!parseWeatherForm(body,parsed));
  in={"1","beijing","4","a","b","replace","replace"}; assert(applyWeather(defaults,in,decoded));
  in.refresh="61"; assert(applyWeather(defaults,in,decoded));
  in.refresh="15"; in.privateKey="a&sig=x"; assert(applyWeather(defaults,in,decoded));
  assert(weatherUrlEncode("a+b/c=")=="a%2Bb%2Fc%3D");
  assert(weatherSignatureInput(1443079775,"fixture")=="ts=1443079775&ttl=600&uid=fixture");

  float number=7;
  for (const char* text : {"nan","inf","1x","","1e2","--1","."}) assert(!weatherNumber(text,-90,65,number) && number==7);
  assert(weatherNumber("-12.5",-90,65,number) && number==-12.5);
  int64_t utc=0,offset=0;
  assert(weatherEpoch("2026-10-25T04:00:00Z",utc));
  assert(weatherEpoch("2026-10-25T12:00:00+08:00",offset) && offset==utc);
  assert(weatherEpoch("2026-10-24T23:00:00-05:00",offset) && offset==utc);
  assert(!weatherEpoch("2025-02-29T12:00:00Z",offset));
  assert(weatherEpoch("2024-02-29T12:00:00Z",offset));
  assert(!weatherEpoch("2026-10-25T12:00:00+14:01",offset));
  assert(!weatherEpoch("2026-10-25T12:00:00+08:00junk",offset));
  Environment indoor; indoor.accept(25.6f,50,UINT32_MAX-100);
  OutdoorWeather outdoor; outdoor.valid=true; outdoor.temperature=18; outdoor.receivedAt=UINT32_MAX-100;
  assert(indoor.fresh(10) && outdoor.fresh(10));
  assert(!indoor.fresh(16000));
  indoor.accept(25.6f,50,0); outdoor.receivedAt=0; outdoor.sourceAgeSeconds=5400;
  assert(!outdoor.fresh(0));
  auto frame=renderComparison(indoor,outdoor,0);
  assert(std::string(frame.lines[1].data()).find("IN:25.6 OUT:18.0C")!=std::string::npos);
  assert(frame.lines[1][19]=='*');
  frame=renderComparison({},outdoor,0);
  assert(std::string(frame.lines[1].data()).find("IN:--.-")!=std::string::npos);
  frame=renderComparison(indoor,{},0);
  assert(std::string(frame.lines[1].data()).find("OUT:--.-")!=std::string::npos);
  ContentRotation rotation; rotation.configure({48,5});
  assert(rotation.page()==ContentPage::outdoor);
  rotation.tick(0,true,true); rotation.tick(5000,true,true); assert(rotation.page()==ContentPage::comparison);
  rotation.tick(5001,true,true); rotation.tick(10001,true,true); assert(rotation.page()==ContentPage::outdoor);
  assert(rotation.key()<16);
  std::cout << "PASS: weather credential persistence/secret handling/forms, signing input, timestamps, stale/missing data, side-by-side temperatures and weather rotation\n";
}
