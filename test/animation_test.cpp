#include "clock/display_service.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
using namespace clockapp;

CellFrame page(const char* top, const char* bottom) {
  CellFrame frame; frame.setAscii(0, top); frame.setAscii(1, bottom); return frame;
}
struct Port {
  bool busy = false;
  std::vector<std::vector<uint8_t>> packets;
  size_t write(const uint8_t* data, size_t size) {
    if (busy) return 0;
    packets.emplace_back(data, data + size); busy = true; return size;
  }
  bool drained() const { return !busy; }
  void drain() { busy = false; }
  size_t powerOffs() const {
    size_t count = 0;
    for (const auto& bytes : packets) if (bytes == std::vector<uint8_t>{0x1f,0x28,0x61,0x40,0}) ++count;
    return count;
  }
};
template<class Service> void tick(Service& service, Port& port, uint32_t now) { port.drain(); service.tick(now); }

void glyphAndProtocol() {
  CellFrame chinese;
  std::string text;
  for (size_t i = 0; i < 20; ++i) text += "\xd6\xd0"; // GB2312 中
  assert(chinese.setEncoded(0, text) && chinese.setEncoded(1, text));
  const auto previous = chinese;
  assert(!chinese.setEncoded(0, "\xd6")); assert(chinese == previous);
  assert(!chinese.setEncoded(0, "\xd6\x20")); assert(chinese == previous);
  assert(!chinese.setEncoded(0, text + "A")); assert(chinese == previous);
  assert(!chinese.setEncoded(2, "A"));
  assert(!chinese.setEncoded(0, "A\nB"));
  Port port; Vfd<Port> driver(port);
  assert(!driver.begin(0) && port.packets.empty());
  assert(driver.begin(3));
  const auto setup = port.packets.back();
  assert((std::vector<uint8_t>(setup.begin(), setup.begin() + 5) == std::vector<uint8_t>{0x1f,0x28,0x61,0x40,1}));
  assert(setup[10] == 0 && setup[13] == 0 && setup[16] == 0); // genuine zero parameters
  assert(!driver.show(chinese)); // UART busy: must not cache the rejected frame
  port.drain(); assert(driver.show(chinese));
  assert(port.packets.back().size() == 88);
  assert(port.packets.back()[4] == 0xd6 && port.packets.back()[5] == 0xd0);
  assert(port.packets.back()[44] == 0x1f && port.packets.back()[47] == 2);
  port.drain(); const auto count = port.packets.size();
  assert(driver.show(chinese) && port.packets.size() == count);
  assert(driver.setPower(false)); assert(port.powerOffs() == 1);
  port.drain(); assert(driver.setPower(true));
  port.drain(); assert(driver.show(chinese)); assert(port.packets.back().size() == 88); // power invalidates cache
  Frame unsafe; unsafe.set(0, "A\x1f\nB");
  port.drain(); assert(driver.show(unsafe));
  assert(port.packets.back()[5] == '?' && port.packets.back()[6] == '?');
}

void animationLogic() {
  const auto old = page("ABCDEFGHIJKLMNOPQRST", "12345678901234567890");
  const auto next = page("abcdefghijklmnopqrst", "ZYXWVUTSRQPONMLKJIHG");
  Animator animation; animation.committed(old);
  animation.start(Effect::pushLeft, next, 100, 400, 2);
  assert(animation.sample(100).rows[1] == old.rows[1]);
  assert(animation.sample(100).rows[0] == next.rows[0]); // untouched row always has current time
  auto half = animation.sample(300);
  assert(half.rows[1][0] == old.rows[1][10] && half.rows[1][10] == next.rows[1][0]);
  auto latest = next; latest.setAscii(1, "latest value");
  animation.target(latest);
  assert(animation.sample(500) == latest); // data updates do not restart transitions
  animation.committed(half);
  animation.start(Effect::centerReveal, latest, 350, 400);
  assert(animation.sample(350) == half); // interruption starts at the committed image
  assert(animation.sample(750) == latest);
  animation.committed(old);
  animation.start(Effect::sleep, CellFrame{}, 0, 400);
  assert(animation.sample(100).rows[0] == old.rows[0]);
  assert(animation.sample(200).rows[1] == CellFrame{}.rows[1]);
  assert(animation.sample(200).rows[0] == old.rows[0]);
  assert(animation.sample(400) == CellFrame{});
  const uint32_t start = UINT32_MAX - 100;
  animation.start(Effect::reveal, next, start, 400);
  assert(animation.active(start + 399) && !animation.active(start + 400));
  assert(animation.sample(start + 400) == next);
  const auto placeholder = page("TIME", "T:--.-C RH:--.-%");
  const auto measured = page("TIME", "T:24.6C RH:51.2%");
  animation.committed(placeholder);
  animation.start(Effect::revealFields, measured, 0, 400, 2);
  assert(animation.sample(100).rows[1][2] == Cell::ascii('-'));
  assert(animation.sample(160).rows[1][2] == Cell::ascii('2'));
  // A GB2312 cell moves as a unit in every sampled frame.
  CellFrame gb; assert(gb.setEncoded(1, "\xd6\xd0\xce\xc4"));
  animation.committed(CellFrame{}); animation.start(Effect::pushLeft, gb, 0, 400, 2);
  for (uint32_t now = 0; now <= 400; ++now) {
    const auto sampled = animation.sample(now);
    for (const auto& cell : sampled.rows[1])
      assert((cell.first() < 0xa1 && !cell.second()) || (cell.first() >= 0xa1 && cell.second() >= 0xa1));
  }
}

void sleepAndWake() {
  Port port; DisplayService<Port> screen(port, Motion::off, 100);
  const auto first = page("12:34:56", "T:24.6C RH:51.2%");
  screen.page(first, 0, 0); screen.begin(2); screen.tick(0);
  tick(screen, port, 1); assert(screen.visible() == first);
  screen.sleep(2); screen.tick(2); assert(port.powerOffs() == 0); // previous frame in flight
  tick(screen, port, 2); assert(screen.visible() == CellFrame{} && port.powerOffs() == 0);
  screen.tick(3); assert(port.powerOffs() == 0); // blank frame still in flight
  tick(screen, port, 3); assert(port.powerOffs() == 1);
  assert(!screen.asleepAndDrained()); port.drain(); assert(screen.asleepAndDrained());
  auto latest = first; latest.setAscii(0, "12:35:01");
  screen.page(latest, 0, 4); tick(screen, port, 10);
  assert(screen.visible() == CellFrame{}); // asleep does not send content
  screen.brightness(4); screen.wake(20); screen.tick(20);
  assert(screen.state() == DisplayService<Port>::State::waking);
  tick(screen, port, 50); // restore packet drained; recovery timing begins now
  screen.tick(149); assert(screen.visible() == CellFrame{});
  screen.tick(150); assert(screen.visible() == latest);
  assert(port.packets.back().size() == 48); // full repaint after wake
  // Both restore and a pending power-off must drain before a new state acts.
  screen.sleep(160); tick(screen, port, 250); tick(screen, port, 251);
  const auto offCount = port.powerOffs();
  screen.wake(252); screen.sleep(253); // cancel wake before ON has been sent
  assert(screen.state() == DisplayService<Port>::State::asleep);
  tick(screen, port, 400); assert(port.powerOffs() == offCount);
}

void serviceRollover() {
  Port port; DisplayService<Port> screen(port, Motion::reduced, 0);
  const uint32_t start = UINT32_MAX - 100;
  const auto content = page("TIME", "latest data");
  screen.page(content, 0, start);
  assert(!screen.begin(0)); screen.tick(start); assert(port.packets.empty());
  assert(screen.begin(2)); screen.tick(start);
  tick(screen, port, start + 301); tick(screen, port, start + 501);
  assert(screen.visible() == content);
  screen.sleep(start + 502); tick(screen, port, start + 702); tick(screen, port, start + 703);
  assert(port.powerOffs() == 1);
  screen.wake(start + 704); tick(screen, port, start + 704);
  tick(screen, port, start + 705); tick(screen, port, start + 805);
  assert(screen.visible() == content);
}

void bootInterruptAndBackpressure() {
  Port port; DisplayService<Port> screen(port);
  const auto content = page("12:34:56", "temperature");
  screen.page(content, 0, 0); screen.begin(2); screen.tick(0);
  tick(screen, port, 400);
  assert(screen.state() == DisplayService<Port>::State::boot);
  tick(screen, port, 800);
  assert(screen.visible().rows[0][5] == Cell::ascii('V'));
  tick(screen, port, 1000); tick(screen, port, 1320);
  assert(screen.visible() == content);
  assert(!screen.pageVisible(0, 1320)); port.drain(); assert(screen.pageVisible(0, 1321));
  screen.sleep(1400); tick(screen, port, 1500);
  assert(screen.visible().rows[0] == content.rows[0]);
  screen.wake(1550); tick(screen, port, 1600); tick(screen, port, 1800);
  assert(screen.visible() == content && port.powerOffs() == 0);
  const auto next = page("12:34:57", "weather");
  screen.page(next, 1, 1900);
  assert(!screen.pageVisible(1, 1900));
  // Emulate a stalled UART: advancing time must not queue stale frames.
  port.busy = true;
  const auto before = port.packets.size(); screen.tick(2000); screen.tick(2100);
  assert(port.packets.size() == before);
  auto newest = next; newest.setAscii(1, "weather updated");
  screen.page(newest, 1, 2200);
  tick(screen, port, 2400);
  assert(screen.visible() == newest && port.packets.size() == before + 1);
  assert(!screen.pageVisible(1, 2400)); port.drain(); assert(screen.pageVisible(1, 2401));
}
int main() {
  glyphAndProtocol(); animationLogic(); sleepAndWake(); serviceRollover(); bootInterruptAndBackpressure();
  std::cout << "PASS: glyph boundaries, protocol zeros, animation coalescing/interruption, boot, sleep drain, wake repaint, UART backpressure\n";
}
