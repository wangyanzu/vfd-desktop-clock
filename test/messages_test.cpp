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
  assert(box.receive("new") == Result::added && box.count() == 8 && box.evicted()==1);
  assert(box.receive("new",0) == Result::invalid);
  assert(box.rejected() == 1 && box.duplicates() == 1);
  auto visible = box.view(clock, false, 0);
  assert(!visible.message && !box.visibleId());
  assert(!box.acknowledge(box.frontId(), true, 0)); // cannot clear a never-visible message
  visible = box.view(clock, true, 10);
  assert(visible.message && visible.id == 17 && visible.frame.rows[0][18] == Cell::ascii('8'));
  assert(!box.acknowledge(visible.id + 1, true, 11));
  assert(!box.acknowledge(visible.id, false, 11));
  assert(box.acknowledge(visible.id, true, 12) && box.count() == 7);
  assert(box.view(clock, true, 13).key == 2 && box.confirming(13));
  auto next = box.view(clock, true, 1012);
  assert(next.message && next.id == 18);
  assert(!box.acknowledge(16, true, 1013));
  assert(box.receive("message0") == Result::added); // acknowledged text may legitimately arrive again
  assert(box.count() == 8);
  MessageInbox scroll;
  assert(scroll.receive("ABCDEFGHIJKLMNOPQRSTUV") == Result::added);
  assert(scroll.view(clock,true,0).frame.rows[1][0] == Cell::ascii('A'));
  assert(scroll.view(clock,true,2499).frame.rows[1][0] == Cell::ascii('A'));
  assert(scroll.view(clock,true,2500).frame.rows[1][0] == Cell::ascii('B'));
  assert(scroll.view(clock,true,4000).frame.rows[1][0] == Cell::ascii('C'));
  assert(scroll.view(clock,true,4999).frame.rows[1][0] == Cell::ascii('C'));
  assert(scroll.view(clock,true,5000).frame.rows[1][0] == Cell::ascii('A'));
  scroll.view(clock,false,5100);
  assert(!scroll.visibleId() && !scroll.acknowledge(scroll.frontId(),true,5100));
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
  for (uint8_t level : {1,2}) {
    const uint32_t duration=level==1?15000:60000;
    MessageInbox box; box.receive("ABCDEFGHIJKLMNOPQRSTUV",level);
    box.view(base,true,0,false); box.view(base,true,100000,false);
    assert(box.count()==1); // entry/backpressure consumes no time
    box.view(base,true,200000,true);
    for (uint32_t play=1;play<duration/5000;++play) {
      const auto frame=box.view(base,true,200000+play*5000,false);
      assert(frame.frame.rows[1][0]==Cell::ascii('A') && box.count()==1);
    }
    box.view(base,true,200000+duration-1); assert(box.count()==1);
    assert(!box.view(base,true,200000+duration).message && !box.count());
    assert(!box.confirming(200000+duration));
  }
  MessageInbox queued; queued.receive("first"); queued.receive("next",2);
  const auto old=queued.frontId(); queued.view(base,true,0);
  const auto next=queued.view(base,true,15000);
  assert(queued.count()==1 && next.id!=old && next.message && queued.frontLevel()==2);
  assert(!queued.acknowledge(old,true,15000));
  queued.view(base,true,100000,false); assert(queued.count()==1);
  queued.view(base,true,101000,true);
  queued.view(base,true,160999); assert(queued.count()==1);
  queued.view(base,true,161000); assert(!queued.count());

  MessageInbox paused; paused.receive("short");
  paused.view(base,true,0); paused.view(base,true,7000);
  paused.view(base,false,7100); paused.view(base,true,100000);
  paused.view(base,true,107999); assert(paused.count()==1);
  paused.view(base,true,108000); assert(!paused.count()); // partial play survives pause

  MessageInbox critical; critical.receive("critical",3);
  const auto id=critical.frontId(); assert(critical.hasCritical());
  critical.view(base,true,0); critical.view(base,true,60000);
  critical.view(base,true,3600000); critical.view(base,true,UINT32_MAX-100);
  assert(critical.count()==1 && critical.frontLevel()==3);
  assert(critical.acknowledge(id,true,200) && !critical.hasCritical());

  MessageInbox rollover; rollover.receive("short");
  const uint32_t start=UINT32_MAX-1000;
  rollover.view(base,true,start);
  rollover.view(base,true,start+14999); assert(rollover.count()==1);
  rollover.view(base,true,start+15000); assert(!rollover.count());
  assert(rollover.receive("invalid level",4)==MessageInbox::Received::invalid);
}
void overflowTests() {
  using R=MessageInbox::Received;
  CellFrame base;
  MessageInbox mixed;
  for (unsigned i=0;i<8;++i) assert(mixed.receive("mixed"+std::to_string(i),(i==1 || i==4)?1:2)==R::added);
  const auto current=mixed.view(base,true,0).id;
  assert(mixed.receive("urgent",3)==R::added && mixed.frontId()==current && mixed.evicted()==1);
  assert(mixed.receive("important",2)==R::added && mixed.evicted()==2);
  assert(mixed.receive("less important",1)==R::full && mixed.evicted()==2);
  assert(mixed.receive("urgent",3)==R::duplicate && mixed.evicted()==2);
  assert(!mixed.acknowledge(current+1,true,1)); // the first low-level entry was evicted
  assert(mixed.acknowledge(current,true,1));
  assert(mixed.view(base,true,1001).id==current+2); // retained FIFO order, removed middle entry
  assert(mixed.hasCritical());

  MessageInbox severe;
  for (unsigned i=0;i<8;++i) severe.receive("critical"+std::to_string(i),3);
  const auto first=severe.view(base,true,0).id;
  for (uint8_t level : {1,2,3}) assert(severe.receive("incoming",level)==R::full);
  assert(severe.frontId()==first && severe.count()==8 && !severe.evicted());
  assert(severe.acknowledge(first,true,1));
  assert(severe.receive("incoming",3)==R::added && severe.count()==8);

  MessageInbox rolling;
  for (unsigned i=0;i<8;++i) rolling.receive("old"+std::to_string(i));
  const auto old=rolling.view(base,true,0).id;
  for (unsigned i=0;i<40;++i) assert(rolling.receive("fresh"+std::to_string(i))==R::added);
  assert(rolling.count()==8 && rolling.evicted()==40 && rolling.frontId()==old+40);
  rolling.view(base,true,100);
  assert(!rolling.acknowledge(old,true,100)); // stale confirmation must not clear replacement
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
  encodingTests(); inboxTests(); playbackTests(); overflowTests(); ackAndRetryTests();
  std::cout << "PASS: all 7445 GB2312 mappings, strict UTF-8, bounded FIFO/dedup, scroll/pause/ack, level deadlines/critical persistence/entry gating, stale IDs and retry rollover\n";
}
