#pragma once
#include "clock/settings.h"
#include <utility>

namespace clockapp {
struct MqttSettings {
  char host[64] = "bemfa.com";
  char topic[65]{}, clientId[65]{}, username[65]{}, password[65]{};
  uint16_t port = 9503;
  bool enabled = false, tls = true;
};
inline bool validMqttSettings(const MqttSettings& s) {
  for (auto pair : {std::pair<const char*, size_t>{s.host, sizeof(s.host)}, {s.topic, sizeof(s.topic)},
                    {s.clientId, sizeof(s.clientId)}, {s.username, sizeof(s.username)}, {s.password, sizeof(s.password)}})
    if (!std::memchr(pair.first, 0, pair.second) || !printable(pair.first) || !validUtf8(pair.first)) return false;
  return validHost(s.host) && s.port && !std::strchr(s.topic, '+') && !std::strchr(s.topic, '#') &&
         (!s.enabled || (s.topic[0] && s.clientId[0]));
}
inline bool sameMqtt(const MqttSettings& a, const MqttSettings& b) {
  return a.port == b.port && a.enabled == b.enabled && a.tls == b.tls &&
    std::strcmp(a.host,b.host)==0 && std::strcmp(a.topic,b.topic)==0 &&
    std::strcmp(a.clientId,b.clientId)==0 && std::strcmp(a.username,b.username)==0 && std::strcmp(a.password,b.password)==0;
}
// Public state deliberately exposes only presence flags for both secrets.
inline std::string mqttPublicJson(const MqttSettings& s) {
  return "{\"enabled\":" + std::string(s.enabled ? "true" : "false") +
    ",\"host\":" + jsonQuote(s.host) + ",\"port\":" + std::to_string(s.port) +
    ",\"tls\":" + (s.tls ? "true" : "false") + ",\"topic\":" + jsonQuote(s.topic) +
    ",\"username\":" + jsonQuote(s.username) + ",\"clientIdSet\":" + (s.clientId[0] ? "true" : "false") +
    ",\"passwordSet\":" + (s.password[0] ? "true" : "false") + "}";
}
// Separate versioned blob: existing Wi-Fi/time settings need no migration.
using MqttSettingsRecord = std::array<uint8_t, 336>;
inline MqttSettingsRecord encodeMqttSettings(const MqttSettings& s) {
  MqttSettingsRecord out{};
  std::memcpy(out.data(), "MQC\1", 4);
  size_t offset=4;
  for (auto pair : {std::pair<const char*, size_t>{s.host,64}, {s.topic,65}, {s.clientId,65}, {s.username,65}, {s.password,65}}) {
    std::memcpy(out.data()+offset,pair.first,pair.second); offset+=pair.second;
  }
  out[328]=static_cast<uint8_t>(s.port); out[329]=s.port>>8;
  out[330]=s.enabled; out[331]=s.tls;
  const auto checksum=crc32(out.data(),332);
  for(unsigned i=0;i<4;++i) out[332+i]=static_cast<uint8_t>(checksum>>(8*i));
  return out;
}
inline bool decodeMqttSettings(const MqttSettingsRecord& record, MqttSettings& destination) {
  if (std::memcmp(record.data(),"MQC\1",4)!=0) return false;
  uint32_t expected=0;
  for(unsigned i=0;i<4;++i) expected|=uint32_t(record[332+i])<<(8*i);
  if (expected!=crc32(record.data(),332) || record[330]>1 || record[331]>1) return false;
  MqttSettings next;
  size_t offset=4;
  for(auto pair : {std::pair<char*,size_t>{next.host,64}, {next.topic,65}, {next.clientId,65}, {next.username,65}, {next.password,65}}) {
    std::memcpy(pair.first,record.data()+offset,pair.second); offset+=pair.second;
  }
  next.port=uint16_t(record[328])|(uint16_t(record[329])<<8);
  next.enabled=record[330]; next.tls=record[331];
  if(!validMqttSettings(next)) return false;
  destination=next; return true;
}
struct MqttSettingsInput { std::string enabled,host,port,tls,topic,username,clientId,clientIdMode,password,passwordMode; };
inline bool parseMqttForm(std::string_view body, MqttSettingsInput& output) {
  if(body.empty() || body.size()>2048) return false;
  MqttSettingsInput candidate;
  const char* names[]={"enabled","host","port","tls","topic","username","client_id","client_id_mode","password","password_mode"};
  std::string* fields[]={&candidate.enabled,&candidate.host,&candidate.port,&candidate.tls,&candidate.topic,&candidate.username,
                         &candidate.clientId,&candidate.clientIdMode,&candidate.password,&candidate.passwordMode};
  unsigned seen=0;
  while(!body.empty()) {
    const auto end=body.find('&'), equal=body.substr(0,end).find('=');
    if(equal==std::string_view::npos) return false;
    std::string key,value;
    const auto part=body.substr(0,end);
    if(!decodeFormValue(part.substr(0,equal),key)||!decodeFormValue(part.substr(equal+1),value)) return false;
    size_t index=0; for(;index<10 && key!=names[index];++index) {}
    if(index==10 || (seen&(1u<<index))) return false;
    *fields[index]=value; seen|=1u<<index;
    if(end==std::string_view::npos) break;
    body.remove_prefix(end+1); if(body.empty()) return false;
  }
  if(seen!=1023) return false;
  output=candidate; return true;
}
inline const char* applyMqttSettings(const MqttSettings& current,const MqttSettingsInput& input,MqttSettings& destination) {
  if((input.enabled!="0" && input.enabled!="1") || (input.tls!="0" && input.tls!="1")) return "请选择有效的启用和加密选项。";
  int port=0;
  if(!validHost(input.host)||!parseInteger(input.port,1,65535,port)) return "请填写有效服务器和 1–65535 范围内的端口。";
  if(input.topic.size()>64 || input.username.size()>64 || !printable(input.topic)||!validUtf8(input.topic)||
     !printable(input.username)||!validUtf8(input.username)||input.topic.find_first_of("+#")!=std::string::npos)
    return "主题与用户名最多 64 字节，且主题不能包含通配符。";
  MqttSettings next=current;
  next.port=static_cast<uint16_t>(port); next.tls=input.tls=="1"; next.enabled=input.enabled=="1";
  const bool changedServer=input.host!=current.host || next.port!=current.port || next.tls!=current.tls;
  const auto copy=[](char* target,size_t capacity,const std::string& value) { std::memset(target,0,capacity); std::memcpy(target,value.data(),value.size()); };
  const auto secret=[&](char* target,const std::string& mode,const std::string& value)->bool {
    if(mode=="keep") return value.empty() && (!changedServer || !target[0]);
    if(mode=="clear") { if(!value.empty()) return false; std::memset(target,0,65); return true; }
    if(mode!="replace"||value.empty()||value.size()>64||!printable(value)||!validUtf8(value)) return false;
    copy(target,65,value); return true;
  };
  if(!secret(next.clientId,input.clientIdMode,input.clientId)||!secret(next.password,input.passwordMode,input.password))
    return "凭据操作无效；更换服务器或加密方式时，请重新填写或明确清除旧凭据。";
  copy(next.host,sizeof(next.host),input.host); copy(next.topic,sizeof(next.topic),input.topic); copy(next.username,sizeof(next.username),input.username);
  if(!validMqttSettings(next)) return "启用 MQTT 时须填写主题和 ClientID。";
  destination=next; return nullptr;
}
}
