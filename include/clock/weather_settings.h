#pragma once
#include "clock/settings.h"
#include <utility>

namespace clockapp {
struct WeatherSettings {
  char publicKey[65]{}, privateKey[65]{}, location[65]{};
  uint16_t refreshMinutes = 15;
  bool enabled = false;
};
inline bool validWeatherKey(std::string_view key) {
  if (key.size() > 64) return false;
  for (char c : key) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  return true;
}
inline bool validWeatherSettings(const WeatherSettings& s) {
  for (const auto p : {s.publicKey, s.privateKey, s.location}) if (!std::memchr(p, 0, 65)) return false;
  return validWeatherKey(s.publicKey) && validWeatherKey(s.privateKey) && printable(s.location) &&
      validUtf8(s.location) && s.refreshMinutes >= 5 && s.refreshMinutes <= 60 &&
      (!s.enabled || (s.publicKey[0] && s.privateKey[0] && s.location[0]));
}
inline bool sameWeather(const WeatherSettings& a, const WeatherSettings& b) {
  return a.enabled == b.enabled && a.refreshMinutes == b.refreshMinutes &&
      std::strcmp(a.publicKey,b.publicKey)==0 && std::strcmp(a.privateKey,b.privateKey)==0 && std::strcmp(a.location,b.location)==0;
}
inline std::string weatherPublicJson(const WeatherSettings& s) {
  return "{\"enabled\":" + std::string(s.enabled ? "true" : "false") + ",\"location\":" + jsonQuote(s.location) +
      ",\"refreshMinutes\":" + std::to_string(s.refreshMinutes) + ",\"publicKeySet\":" + (s.publicKey[0] ? "true" : "false") +
      ",\"privateKeySet\":" + (s.privateKey[0] ? "true" : "false") + "}";
}
using WeatherRecord = std::array<uint8_t,206>;
inline WeatherRecord encodeWeather(const WeatherSettings& s) {
  WeatherRecord r{}; std::memcpy(r.data(),"WTH\1",4);
  std::memcpy(r.data()+4,s.publicKey,65); std::memcpy(r.data()+69,s.privateKey,65); std::memcpy(r.data()+134,s.location,65);
  r[199]=s.enabled; r[200]=uint8_t(s.refreshMinutes); r[201]=uint8_t(s.refreshMinutes >> 8);
  const auto crc=crc32(r.data(),202);
  for (unsigned i=0;i<4;++i) r[202+i]=uint8_t(crc >> (i*8));
  return r;
}
inline bool decodeWeather(const WeatherRecord& r, WeatherSettings& out) {
  uint32_t crc=0; for (unsigned i=0;i<4;++i) crc|=uint32_t(r[202+i]) << (i*8);
  if (std::memcmp(r.data(),"WTH\1",4) || crc!=crc32(r.data(),202) || r[199]>1) return false;
  WeatherSettings s;
  std::memcpy(s.publicKey,r.data()+4,65); std::memcpy(s.privateKey,r.data()+69,65); std::memcpy(s.location,r.data()+134,65);
  s.enabled=r[199]; s.refreshMinutes=uint16_t(r[200]) | uint16_t(r[201]) << 8;
  if (!validWeatherSettings(s)) return false;
  out=s; return true;
}
struct WeatherInput { std::string enabled, location, refresh, publicKey, privateKey, publicMode, privateMode; };
inline bool parseWeatherForm(std::string_view body, WeatherInput& out) {
  if (body.empty() || body.size()>1024) return false;
  WeatherInput s;
  const char* names[]={"enabled","location","refresh","public_key","private_key","public_mode","private_mode"};
  std::string* fields[]={&s.enabled,&s.location,&s.refresh,&s.publicKey,&s.privateKey,&s.publicMode,&s.privateMode};
  unsigned seen=0;
  while (!body.empty()) {
    const auto end=body.find('&'); const auto part=body.substr(0,end); const auto eq=part.find('=');
    if (eq==std::string_view::npos) return false;
    std::string key,value;
    if (!decodeFormValue(part.substr(0,eq),key) || !decodeFormValue(part.substr(eq+1),value)) return false;
    unsigned i=0; while (i<7 && key!=names[i]) ++i;
    if (i==7 || (seen & (1u << i))) return false;
    seen |= 1u << i; *fields[i]=value;
    if (end==std::string_view::npos) break;
    body.remove_prefix(end+1); if (body.empty()) return false;
  }
  if (seen!=127) return false;
  out=s; return true;
}
inline const char* applyWeather(const WeatherSettings& current, const WeatherInput& input, WeatherSettings& out) {
  int minutes=0;
  if ((input.enabled!="0" && input.enabled!="1") || !parseInteger(input.refresh,5,60,minutes) ||
      input.location.size()>64 || !printable(input.location) || !validUtf8(input.location)) return "Invalid weather settings";
  WeatherSettings s=current; s.enabled=input.enabled=="1"; s.refreshMinutes=uint16_t(minutes);
  std::memset(s.location,0,65); std::memcpy(s.location,input.location.data(),input.location.size());
  const auto secret=[](char* target,const std::string& mode,const std::string& value) {
    if (mode=="keep") return value.empty();
    if (mode=="clear") { if (!value.empty()) return false; std::memset(target,0,65); return true; }
    if (mode!="replace" || value.empty() || !validWeatherKey(value)) return false;
    std::memset(target,0,65); std::memcpy(target,value.data(),value.size()); return true;
  };
  if (!secret(s.publicKey,input.publicMode,input.publicKey) || !secret(s.privateKey,input.privateMode,input.privateKey))
    return "Invalid key operation";
  if (!validWeatherSettings(s)) return "Location and both keys are required when enabled";
  out=s; return nullptr;
}
inline std::string weatherUrlEncode(std::string_view input) {
  const char* hex="0123456789ABCDEF"; std::string out;
  for (unsigned char c : input) {
    if ((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.' || c=='~') out+=char(c);
    else { out+='%'; out+=hex[c>>4]; out+=hex[c&15]; }
  }
  return out;
}
inline std::string weatherSignatureInput(uint64_t epoch,const char* publicKey) {
  return "ts=" + std::to_string(epoch) + "&ttl=600&uid=" + publicKey;
}
}
