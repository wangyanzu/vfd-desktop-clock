#include "clock/messages.h"
#include "clock/retry.h"
#include <cassert>
#include <iostream>
#include <string>
using namespace clockapp;
std::string utf8(uint16_t point) {
  if (point < 0x800) return {char(0xc0 | (point >> 6)), char(0x80 | (point & 63))};
  return {char(0xe0 | (point >> 12)), char(0x80 | ((point >> 6) & 63)), char(0x80 | (point & 63))};
}
void encodingTests() {
  DisplayText text;
  assert(encodeMessage("温度25°C", text) && text.length == 6);
  assert(text.cells[0].first() == 0xce && text.cells[0].second() == 0xc2);
  assert(text.cells[1].first() == 0xb6 && text.cells[1].second() == 0xc8);
  assert(text.substitutions == 0);
  uint16_t last = 0;
  for (const auto& mapping : encoding::gb2312) {
    assert(mapping.unicode > last); last = mapping.unicode;
    assert(encodeMessage(utf8(mapping.unicode), text) && text.length == 1);
    assert(text.cells[0].first() == (mapping.gb >> 8) && text.cells[0].second() == (mapping.gb & 255));
  }
  assert(encodeMessage("hi😀", text) && text.length == 3 && text.substitutions == 1);
  assert(text.cells[2] == Cell::ascii('?'));
  const auto unchanged = text;
  assert(!encodeMessage(std::string_view("ok\0bad",6), text));
  assert(!encodeMessage("\x1b@", text));
  assert(!encodeMessage("\xc0\x80", text));
  assert(!encodeMessage("\xed\xa0\x80", text));
  assert(!encodeMessage("\xf4\x90\x80\x80", text));
  assert(!encodeMessage("\xf0\x9f", text));
  assert(!encodeMessage(std::string(129,'A'), text));
  assert(!encodeMessage(std::string(257,'A'), text));
  assert(!encodeMessage("\n \t\r", text));
  assert(text.length == unchanged.length && text.cells == unchanged.cells);
  assert(encodeMessage("A\nB\tC", text) && text.cells[1] == Cell{} && text.cells[3] == Cell{});
  assert(encodeMessage(std::string(128,'A'), text) && text.length == 128);
}
void inboxTests() {
  using Result = MessageInbox::Received;
  MessageInbox box;
  const CellFrame clock(Frame{});
  for (unsigned i = 0; i < 8; ++i) assert(box.receive("message" + std::to_string(i)) == Result::added);
  assert(box.receive("message0") == Result::duplicate);
  assert(box.receive("new") == Result::full && box.count() == 8);
  assert(box.rejected() == 1 && box.duplicates() == 1);
  auto visible = box.view(clock, false, 0);
  assert(!visible.message && !box.visibleId());
  assert(!box.acknowledge(box.frontId(), true, 0)); // cannot clear a never-visible message
  visible = box.view(clock, true, 10);
  assert(visible.message && visible.id == 16 && visible.frame.rows[0][18] == Cell::ascii('8'));
  assert(!box.acknowledge(visible.id + 1, true, 11));
  assert(!box.acknowledge(visible.id, false, 11));
  assert(box.acknowledge(visible.id, true, 12) && box.count() == 7);
  assert(box.view(clock, true, 13).key == 2 && box.confirming(13));
  auto next = box.view(clock, true, 1012);
  assert(next.message && next.id == 17);
  assert(!box.acknowledge(16, true, 1013));
  assert(box.receive("message0") == Result::added); // acknowledged text may legitimately arrive again
  assert(box.count() == 8);
  MessageInbox scroll;
  assert(scroll.receive("ABCDEFGHIJKLMNOPQRSTUV") == Result::added);
  assert(scroll.view(clock,true,0).frame.rows[1][0] == Cell::ascii('A'));
  assert(scroll.view(clock,true,1899).frame.rows[1][0] == Cell::ascii('A'));
  assert(scroll.view(clock,true,1900).frame.rows[1][0] == Cell::ascii('B'));
  assert(scroll.view(clock,true,2300).frame.rows[1][0] == Cell::ascii('C'));
  assert(scroll.view(clock,true,3499).frame.rows[1][0] == Cell::ascii('C'));
  assert(scroll.view(clock,true,3500).frame.rows[1][0] == Cell::ascii('A'));
  scroll.view(clock,false,3600);
  assert(!scroll.visibleId() && !scroll.acknowledge(scroll.frontId(),true,3600));
  assert(scroll.view(clock,true,10000).frame.rows[1][0] == Cell::ascii('A'));
  MessageInbox chinese;
  assert(chinese.receive("这是完整的中文消息滚动测试，每一个汉字都必须保持完整不能拆开。") == Result::added);
  const auto wrapped = UINT32_MAX - 100;
  chinese.view(clock,true,wrapped);
  const auto shifted = chinese.view(clock,true,wrapped+2300);
  assert(shifted.frame.rows[1][0].second() != 0);
  assert(chinese.acknowledge(shifted.id,true,wrapped+2400));
  assert(chinese.view(clock,true,wrapped+2500).key == 2);
  assert(!chinese.view(clock,true,wrapped+3400).message);
}
void playbackTests() {
  CellFrame base;
  MessageInbox shortMessage;
  shortMessage.receive("short");
  shortMessage.view(base,true,0,false);
  shortMessage.view(base,true,100000,false);
  assert(shortMessage.count()==1); // entry/backpressure consumes no plays
  shortMessage.view(base,true,200000,true);
  shortMessage.view(base,true,249999,false); // ordinary redraws do not restart timing
  assert(shortMessage.count()==1);
  assert(!shortMessage.view(base,true,250000,true).message && shortMessage.count()==0);
  assert(!shortMessage.confirming(250000)); // expiry is not reported as user confirmation

  MessageInbox longMessage;
  longMessage.receive("ABCDEFGHIJKLMNOPQRSTUV"); // cycle = 3500 ms
  longMessage.receive("next");
  const auto old=longMessage.frontId();
  longMessage.view(base,true,0);
  longMessage.view(base,true,34999);
  assert(longMessage.count()==2);
  auto next=longMessage.view(base,true,35000);
  assert(longMessage.count()==1 && next.id!=old && next.message);
  assert(!longMessage.acknowledge(old,true,35000));
  longMessage.view(base,true,100000,false);
  assert(longMessage.count()==1); // the second message waits for its own entry
  longMessage.view(base,true,101000,true);
  longMessage.view(base,true,150999,true); assert(longMessage.count()==1);
  longMessage.view(base,true,151000,true); assert(longMessage.count()==0);

  MessageInbox paused;
  paused.receive("ABCDEFGHIJKLMNOPQRSTUV");
  paused.view(base,true,0);
  paused.view(base,true,31500); // nine complete plays
  paused.view(base,false,31600);
  paused.view(base,true,100000); // resume: restart unfinished play, keep nine complete
  paused.view(base,true,103499); assert(paused.count()==1);
  paused.view(base,true,103500); assert(paused.count()==0);

  MessageInbox rollover;
  rollover.receive("short");
  const uint32_t start=UINT32_MAX-1000;
  rollover.view(base,true,start);
  rollover.view(base,true,start+49999); assert(rollover.count()==1);
  rollover.view(base,true,start+50000); assert(rollover.count()==0);
}
void ackAndRetryTests() {
  uint32_t id = 0;
  assert(parseMessageAck("id=16",id) && id == 16);
  assert(parseMessageAck("id=4294967295",id) && id == UINT32_MAX);
  for (const auto body : {"id=0","id=15","id=-16","id=4294967296","id=16&id=17","id=%31%36","id=16x","id="})
    assert(!parseMessageAck(body,id));
  RetrySchedule retry; assert(retry.ready(0));
  const auto start = UINT32_MAX - 100;
  retry.failed(start); assert(!retry.ready(start+999)); assert(retry.ready(start+1000));
  retry.failed(start+1000); assert(!retry.ready(start+2999)); assert(retry.ready(start+3000));
  uint32_t now = start+3000;
  for (unsigned i=0;i<8;++i) { retry.failed(now); now += 60000; assert(retry.ready(now)); }
  retry.reset(); assert(retry.ready(now));
}
int main() {
  encodingTests(); inboxTests(); playbackTests(); ackAndRetryTests();
  std::cout << "PASS: all 7445 GB2312 mappings, strict UTF-8, bounded FIFO/dedup, scroll/pause/ack, ten-play expiry/entry gating, stale IDs and retry rollover\n";
}
