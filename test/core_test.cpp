#include "clock/core.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <vector>
using namespace clockapp;
struct Sink {
  std::vector<uint8_t> bytes;
  size_t write(const uint8_t* data, size_t size) { bytes.insert(bytes.end(), data, data + size); return size; }
};
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
  tm local{}; local.tm_year = 126; local.tm_mon = 9; local.tm_mday = 21; local.tm_hour = 12;
  local.tm_min = 34; local.tm_sec = 56;
  auto frame = render(&local, env, true, false, 100);
  assert(std::strncmp(frame.lines[0].data(), "2026-10!12:34:56 SUN", 20) == 0);
  assert(frame.lines[0][7] == '!' && frame.lines[1][19] == '*');
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
  std::cout << "PASS: time rollover, frame rendering, VFD bytes/diff and button debounce\n";
}
