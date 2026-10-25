#include "clock/message_payload.h"
#include <cassert>
#include <iostream>
using namespace clockapp;
int main() {
  using R=MessageInbox::Received;
  MessageInbox inbox; CellFrame base;
  assert(receiveMessagePayload(inbox,R"({"l":1,"msg":"hello"})")==R::added);
  assert(inbox.frontLevel()==1);
  assert(receiveMessagePayload(inbox,R"({"msg":"hello","l":1})")==R::duplicate);
  assert(receiveMessagePayload(inbox,"hello")==R::duplicate);
  assert(receiveMessagePayload(inbox,R"({"l":2,"msg":"hello"})")==R::added);
  assert(receiveMessagePayload(inbox,R"({"l":3,"msg":"严重\u6d88息"})")==R::added);
  assert(inbox.hasCritical());
  const auto first=inbox.view(base,true,0);
  assert(first.frame.rows[1][0]==Cell::ascii('h')); // envelope never reaches screen
  inbox.view(base,true,15000); assert(inbox.frontLevel()==2 && inbox.count()==2);
  inbox.view(base,true,15001); inbox.view(base,true,75001);
  assert(inbox.frontLevel()==3 && inbox.count()==1);
  const auto critical=inbox.view(base,true,75002);
  assert(critical.frame.rows[1][0].second()!=0);
  inbox.view(base,true,3600000); assert(inbox.count()==1);
  assert(inbox.acknowledge(critical.id,true,3600001));
  for (const auto bad : {"", "[]", "{", "{}", R"({"l":1})", R"({"msg":"x"})",R"({"l":"1","msg":"x"})",
      R"({"l":0,"msg":"x"})",R"({"l":4,"msg":"x"})",R"({"l":1.5,"msg":"x"})",R"({"l":true,"msg":"x"})",
      R"({"l":1,"msg":""})",R"({"l":1,"msg":1})",R"({"l":1,"msg":"x","l":3})",
      R"({"l":1,"msg":"x","msg":"y"})",R"({"l":1,"msg":"x","extra":1})",R"({"l":1,"msg":"x"}garbage)",
      R"({"l":1,"msg":"x\u0000hidden"})",R"({"l":1,"msg":"\u001b@"})",R"({"l":1,"msg":[[[[[[]]]]]]})"}) {
    MessageInbox badBox; assert(receiveMessagePayload(badBox,bad)==R::invalid);
    assert(!badBox.count() && badBox.rejected()==1);
  }
  assert(receiveMessagePayload(inbox,std::string(1025,'a'))==R::invalid);
  MessageInbox escaped;
  assert(receiveMessagePayload(escaped,R"({"l":1,"msg":"literal \\u0000"})")==R::added);
  MessageInbox maximum;
  const std::string escaped128=std::string("{\"l\":2,\"msg\":\"")+[] {std::string s;for(int i=0;i<128;++i)s+="\\u0041";return s;}()+"\"}";
  assert(escaped128.size()>256 && receiveMessagePayload(maximum,escaped128)==R::added);
  assert(maximum.view(base,true,0).frame.rows[1][19]==Cell::ascii('A'));
  std::cout<<"PASS: JSON levels/text, semantic dedup, legacy text, Chinese/escapes, bounded malformed input and severity expiry\n";
}
