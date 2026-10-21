#include "clock/mqtt_settings.h"
#include <cassert>
#include <iostream>
using namespace clockapp;
int main() {
  MqttSettings original;
  original.enabled=true;
  std::strcpy(original.topic,"桌面消息"); std::strcpy(original.clientId,"fixture-private-key");
  std::strcpy(original.username,"fixture-user"); std::strcpy(original.password,"fixture-password");
  assert(validMqttSettings(original));
  const auto publicJson=mqttPublicJson(original);
  assert(publicJson.find(original.clientId)==std::string::npos && publicJson.find(original.password)==std::string::npos);
  assert(publicJson.find("\"clientIdSet\":true")!=std::string::npos && publicJson.find("\"passwordSet\":true")!=std::string::npos);
  auto record=encodeMqttSettings(original);
  MqttSettings decoded;
  assert(decodeMqttSettings(record,decoded) && sameMqtt(original,decoded));
  for(size_t i=0;i<record.size();++i) {
    auto bad=record; bad[i]^=1; auto target=original;
    assert(!decodeMqttSettings(bad,target) && sameMqtt(target,original));
  }
  auto repair=[](MqttSettingsRecord& value) {
    const auto crc=crc32(value.data(),value.size()-4);
    for(unsigned i=0;i<4;++i) value[value.size()-4+i]=static_cast<uint8_t>(crc>>(8*i));
  };
  auto bad=record; bad[330]=2; repair(bad); assert(!decodeMqttSettings(bad,decoded));
  bad=record; for(size_t i=133;i<198;++i) bad[i]='x'; repair(bad); assert(!decodeMqttSettings(bad,decoded));
  MqttSettingsInput input{"1","bemfa.com","9503","1","new-topic","fixture-user","","keep","","keep"};
  MqttSettings next;
  assert(!applyMqttSettings(original,input,next));
  assert(std::strcmp(next.clientId,original.clientId)==0 && std::strcmp(next.password,original.password)==0);
  input.host="other.example.com";
  next=original;
  assert(applyMqttSettings(original,input,next) && sameMqtt(next,original));
  input.clientIdMode="replace"; input.clientId="new-private-key";
  assert(applyMqttSettings(original,input,next)); // password must also be explicit
  input.passwordMode="clear";
  assert(!applyMqttSettings(original,input,next) && !next.password[0]);
  input.host="bemfa.com"; input.port="9501"; input.tls="0"; input.clientIdMode="keep"; input.clientId="";
  assert(applyMqttSettings(original,input,next));
  input.port="9503"; input.tls="1"; input.passwordMode="replace"; input.password="new-password";
  assert(!applyMqttSettings(original,input,next));
  input.clientIdMode="clear"; assert(applyMqttSettings(original,input,next));
  input.enabled="0"; assert(!applyMqttSettings(original,input,next) && !next.enabled && !next.clientId[0]);
  input.passwordMode="keep"; input.password="should-not-be-sent"; assert(applyMqttSettings(original,input,next));
  input.password=""; input.topic="a/#"; assert(applyMqttSettings(original,input,next));
  input.topic="a"; input.port="65536"; assert(applyMqttSettings(original,input,next));
  input.port="0"; assert(applyMqttSettings(original,input,next));
  input.port="65535"; input.passwordMode="clear"; assert(!applyMqttSettings(original,input,next));
  input.topic=std::string(65,'a'); assert(applyMqttSettings(original,input,next));
  input.topic=std::string("a\0b",3); assert(applyMqttSettings(original,input,next));
  input.topic=std::string("\xff",1); assert(applyMqttSettings(original,input,next));
  input.topic="a"; input.clientIdMode="replace"; input.clientId=std::string("bad\nsecret");
  assert(applyMqttSettings(original,input,next));
  input.clientId=std::string("\xff",1); assert(applyMqttSettings(original,input,next));
  input.clientId=std::string(65,'x'); assert(applyMqttSettings(original,input,next));
  input.clientId=std::string(64,'x'); input.passwordMode="replace"; input.password=std::string(64,'y');
  input.host=std::string(63,'h'); input.topic=std::string(64,'t'); input.username=std::string(64,'u'); input.enabled="1";
  assert(!applyMqttSettings(original,input,next));
  const auto maximum=encodeMqttSettings(next); assert(decodeMqttSettings(maximum,decoded) && sameMqtt(next,decoded));
  const std::string form="enabled=1&host=bemfa.com&port=9503&tls=1&topic=%E6%A1%8C%E9%9D%A2&username=&client_id=&client_id_mode=keep&password=&password_mode=keep";
  assert(parseMqttForm(form,input) && input.topic=="桌面");
  for(auto suffix : {"&host=duplicate","&unknown=value","&"}) assert(!parseMqttForm(form+suffix,input));
  assert(!parseMqttForm("enabled=1",input)); assert(!parseMqttForm(form+"&password=%00",input));
  assert(!parseMqttForm(std::string(2049,'a'),input));
  auto invalid=original; std::memset(invalid.password,'a',65); assert(!validMqttSettings(invalid));
  std::cout<<"PASS: MQTT record integrity, secret preservation, endpoint credential isolation, transactional validation and strict forms\n";
}
