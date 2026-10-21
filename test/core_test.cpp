#include "clock/aht10.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <vector>
using namespace clockapp;
struct Sink {
  std::vector<uint8_t> bytes;
  size_t write(const uint8_t* data, size_t size) { bytes.insert(bytes.end(), data, data + size); return size; }
};
struct Bus : Sink {
  bool ok = true;
  std::vector<uint8_t> reply{0x08};
  bool write(const uint8_t* data, size_t size) {
    Sink::write(data, size);
    return ok;
  }
  bool read(uint8_t* data, size_t size) {
    if (!ok || reply.size() < size) return false;
    std::memcpy(data, reply.data(), size);
    return true;
  }
};
void calibrate(Aht10<Bus>& aht, uint32_t start = 0) {
  aht.begin(start); aht.tick(start + 40); aht.tick(start + 140); aht.tick(start + 141);
}
int main() {
  assert(elapsed(15, UINT32_MAX - 10, 26));
  assert(!elapsed(15, UINT32_MAX - 10, 27));
  Environment env;
  assert(env.accept(25, 50, UINT32_MAX - 10));
  assert(env.fresh(100));
  assert(!env.fresh(16000));
  assert(!env.accept(std::numeric_limits<float>::quiet_NaN(), 50, 100));
  assert(env.valid && !env.healthy && env.temperature == 25);
  assert(!env.accept(25, 101, 100));
  tm local{}; local.tm_mon = 9; local.tm_mday = 21; local.tm_hour = 12;
  local.tm_min = 34; local.tm_sec = 56;
  auto frame = render(&local, env, true, false, 100);
  assert(std::strncmp(frame.lines[0].data(), "10-21 12:34:56", 13) == 0);
  assert(frame.lines[0][19] == '!' && frame.lines[1][19] == '*');
  for (const auto& line : frame.lines) assert(std::strlen(line.data()) == 20);
  assert(std::strncmp(render(nullptr, {}, false, false, 0).lines[0].data(), "WiFi offline", 12) == 0);
  frame.set(0, "01234567890123456789012345");
  assert(std::strlen(frame.lines[0].data()) == 20);
  Sink sink; Vfd<Sink> display(sink);
  display.begin(); sink.bytes.clear();
  assert(!display.setBrightness(0) && !display.setBrightness(5) && sink.bytes.empty());
  display.show(frame);
  assert(sink.bytes.size() == 48);
  assert(sink.bytes[0] == 0x1f && sink.bytes[1] == 0x24 && sink.bytes[3] == 1);
  sink.bytes.clear(); display.show(frame); assert(sink.bytes.empty());
  frame.set(1, "short"); display.show(frame); assert(sink.bytes.size() == 24);
  assert(sink.bytes.back() == ' '); // old content is erased without clearing screen
  Button button;
  button.update(true, 0); button.update(false, 10); button.update(false, 50);
  assert(button.update(false, 60) == Button::Event::none);
  button.update(true, 100); button.update(true, 130);
  button.update(false, 200);
  assert(button.update(false, 230) == Button::Event::shortPress);
  button.update(true, 300); button.update(true, 330);
  assert(button.update(true, 3330) == Button::Event::longPress);
  button.update(false, 3400);
  assert(button.update(false, 3430) == Button::Event::none);
  Bus bus; Aht10<Bus> aht(bus); calibrate(aht);
  assert((bus.bytes == std::vector<uint8_t>{0xe1,0x08,0,0xac,0x33,0}));
  // 50% RH and 25 C: humidity=0x80000, temperature=0x60000.
  bus.reply = {0x08,0x80,0x00,0x06,0x00,0x00};
  aht.tick(221);
  assert(aht.environment().fresh(221));
  assert(aht.environment().temperature == 25 && aht.environment().humidity == 50);
  aht.tick(5221); aht.tick(5222);
  bus.ok = false; aht.tick(5302);
  assert(aht.environment().valid && !aht.environment().healthy);
  const auto oldBytes = bus.bytes.size();
  aht.tick(15301); assert(bus.bytes.size() == oldBytes);
  bus.ok = true; aht.tick(15302);
  assert(bus.bytes.back() == 0xba);
  aht.tick(15342); bus.reply = {0x08}; aht.tick(15442); aht.tick(15443);
  bus.reply = {0x08,0x80,0,0x06,0,0}; aht.tick(15523);
  assert(aht.environment().fresh(15523));
  Bus busy; Aht10<Bus> stuck(busy); calibrate(stuck);
  busy.reply = {0x88,0,0,0,0,0};
  stuck.tick(221); stuck.tick(391);
  assert(!stuck.environment().healthy);
  stuck.tick(10391); assert(busy.bytes.back() == 0xba);
  Bus uncalibrated; Aht10<Bus> rejected(uncalibrated);
  uncalibrated.reply = {0x00}; calibrate(rejected);
  assert((uncalibrated.bytes == std::vector<uint8_t>{0xe1,0x08,0}));
  rejected.tick(10140); assert(uncalibrated.bytes.back() == 0xba);
  Bus truncated; Aht10<Bus> shortRead(truncated); calibrate(shortRead);
  truncated.reply = {0x08}; shortRead.tick(221);
  assert(!shortRead.environment().valid);
  shortRead.tick(10221); assert(truncated.bytes.back() == 0xba);
  Bus nack; Aht10<Bus> absent(nack); nack.ok = false;
  absent.begin(0); absent.tick(40);
  absent.tick(10039); assert(nack.bytes.size() == 3);
  absent.tick(10040); assert(nack.bytes.size() == 4);
  Bus rollover; Aht10<Bus> wrapped(rollover);
  const uint32_t start = UINT32_MAX - 100;
  calibrate(wrapped, start);
  rollover.reply = {0x08,0x80,0,0x06,0,0};
  wrapped.tick(start + 221);
  assert(wrapped.environment().valid);
  std::cout << "PASS: time rollover, frame rendering, VFD bytes/diff, button debounce, AHT10 decoding/timeout/recovery\n";
}
