#pragma once
#include "clock/weather.h"
#include "clock/settings.h"
#include <cJSON.h>

namespace clockapp {
inline bool boundedWeatherJson(const std::string& body) {
  if (body.empty() || body.size()>4096 || body.find('\0')!=std::string::npos) return false;
  unsigned depth=0; bool quoted=false, escaped=false;
  for (char c : body) {
    if (quoted) { if (escaped) escaped=false; else if (c=='\\') escaped=true; else if (c=='"') quoted=false; }
    else if (c=='"') quoted=true;
    else if (c=='{' || c=='[') { if (++depth>16) return false; }
    else if (c=='}' || c==']') { if (!depth) return false; --depth; }
  }
  return !quoted && !depth;
}
inline std::string weatherErrorCode(const std::string& body) {
  if (!boundedWeatherJson(body)) return {};
  cJSON* root=cJSON_ParseWithLengthOpts(body.c_str(),body.size()+1,nullptr,true);
  if (!root) return {};
  std::string code; unsigned count=0;
  for (const cJSON* item=root->child;item;item=item->next) {
    if (item->string && std::strcmp(item->string,"status_code")==0) {
      ++count;
      if (cJSON_IsString(item) && item->valuestring) code=item->valuestring;
    }
  }
  cJSON_Delete(root);
  if (count!=1 || code.size()!=8 || code.substr(0,2)!="AP") return {};
  for (size_t i=2;i<code.size();++i) if (code[i]<'0' || code[i]>'9') return {};
  return code;
}
inline bool parseWeatherResponse(const std::string& body,int64_t epoch,uint32_t now,OutdoorWeather& out) {
  if (!boundedWeatherJson(body)) return false;
  const char* end=nullptr;
  cJSON* root=cJSON_ParseWithLengthOpts(body.c_str(),body.size()+1,&end,true);
  if (!root) return false;
  const auto parse=[&]() {
    const auto unique=[](const cJSON* object) {
      if (!cJSON_IsObject(object)) return false;
      unsigned count=0;
      for (const cJSON* a=object->child;a;a=a->next) {
        if (++count>32 || !a->string) return false;
        for (const cJSON* b=a->next;b;b=b->next) if (b->string && std::strcmp(a->string,b->string)==0) return false;
      }
      return true;
    };
    if (!unique(root)) return false;
    const cJSON* results=cJSON_GetObjectItemCaseSensitive(root,"results");
    if (!cJSON_IsArray(results) || cJSON_GetArraySize(results)!=1) return false;
    const cJSON* item=cJSON_GetArrayItem(results,0);
    const cJSON* current=cJSON_GetObjectItemCaseSensitive(item,"now");
    const cJSON* location=cJSON_GetObjectItemCaseSensitive(item,"location");
    if (!unique(item) || !unique(current) || !unique(location)) return false;
    const auto number=[](const cJSON* object,const char* key,float low,float high,float& value) {
      const cJSON* field=cJSON_GetObjectItemCaseSensitive(object,key);
      if (cJSON_IsString(field)) return weatherNumber(field->valuestring,low,high,value);
      if (!cJSON_IsNumber(field) || !std::isfinite(field->valuedouble) || field->valuedouble<low || field->valuedouble>high) return false;
      value=float(field->valuedouble); return true;
    };
    const auto string=[](const cJSON* object,const char* key,char* dest,size_t capacity) {
      const cJSON* field=cJSON_GetObjectItemCaseSensitive(object,key);
      if (!cJSON_IsString(field) || !field->valuestring || !*field->valuestring || std::strlen(field->valuestring)>=capacity ||
          !printable(field->valuestring) || !validUtf8(field->valuestring)) return false;
      std::strcpy(dest,field->valuestring); return true;
    };
    OutdoorWeather next; float code=0;
    if (!number(current,"temperature",-90,65,next.temperature) || !number(current,"code",0,99,code) || std::floor(code)!=code ||
        !string(current,"text",next.text,sizeof(next.text)) || !string(location,"name",next.city,sizeof(next.city)) ||
        !string(item,"last_update",next.updated,sizeof(next.updated))) return false;
    int64_t observed=0;
    if (!weatherEpoch(next.updated,observed) || observed>epoch+300 || epoch<1704067200) return false;
    const uint64_t age=epoch>observed?uint64_t(epoch-observed):0;
    if (age>UINT32_MAX) return false;
    next.humidityValid=number(current,"humidity",0,100,next.humidity);
    next.code=int(code); next.receivedAt=now; next.sourceAgeSeconds=uint32_t(age); next.valid=true;
    out=next; return true;
  };
  const bool valid=parse(); cJSON_Delete(root); return valid;
}
}
