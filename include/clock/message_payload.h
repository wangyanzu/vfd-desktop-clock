#pragma once
#include "clock/messages.h"
#include <cJSON.h>
#include <string>
namespace clockapp {
// Bounded JSON envelope. The retained message stores decoded text, never JSON.
struct MessagePayload { std::string text; uint8_t level=1; };
inline bool decodeMessagePayload(std::string_view payload, MessagePayload& destination) {
  if (payload.empty() || payload.size()>1024 || payload.find('\0')!=std::string_view::npos) return false;
  const auto first=payload.find_first_not_of(" \r\n\t");
  if (first==std::string_view::npos) return false;
  if (payload[first]!='{' && payload[first]!='[') {
    DisplayText verified;
    if (!encodeMessage(payload,verified)) return false;
    destination={std::string(payload),1}; return true;
  }
  unsigned depth=0; bool quoted=false,escaped=false;
  for (size_t i=0;i<payload.size();++i) {
    const char c=payload[i];
    if (quoted) {
      if (escaped) {
        // cJSON strings expose no byte length; reject embedded NUL before parsing.
        if (c=='u' && payload.substr(i+1,4)=="0000") return false;
        escaped=false;
      } else if (c=='\\') escaped=true;
      else if (c=='"') quoted=false;
    } else if (c=='"') quoted=true;
    else if (c=='{' || c=='[') { if (++depth>4) return false; }
    else if (c=='}' || c==']') { if (!depth) return false; --depth; }
  }
  if (quoted || depth) return false;
  const std::string json(payload);
  cJSON* root=cJSON_ParseWithLengthOpts(json.c_str(),json.size()+1,nullptr,true);
  if (!root) return false;
  const cJSON* level=nullptr; const cJSON* message=nullptr; bool valid=cJSON_IsObject(root);
  for (const cJSON* field=root->child;field && valid;field=field->next) {
    if (field->string && std::strcmp(field->string,"l")==0 && !level) level=field;
    else if (field->string && std::strcmp(field->string,"msg")==0 && !message) message=field;
    else valid=false;
  }
  valid=valid && cJSON_IsNumber(level) && level->valuedouble>=1 && level->valuedouble<=3 &&
      level->valuedouble==level->valueint && cJSON_IsString(message) && message->valuestring;
  DisplayText verified;
  valid=valid && encodeMessage(message->valuestring,verified);
  if (valid) destination={message->valuestring,uint8_t(level->valueint)};
  cJSON_Delete(root); return valid;
}
inline MessageInbox::Received receiveMessagePayload(MessageInbox& inbox, std::string_view payload) {
  MessagePayload decoded;
  if (!decodeMessagePayload(payload,decoded)) return inbox.rejectInvalid();
  return inbox.receive(decoded.text,decoded.level);
}
}
