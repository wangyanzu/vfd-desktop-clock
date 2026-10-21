// Generates a bench preview from the SAME DisplayService used by firmware.
// UART drain times are modeled; this is not physical display verification.
#include "clock/display_service.h"
#include "clock/settings.h"
#include "clock/messages.h"
#include "clock/rotation.h"
#include <iostream>
#include <string>
using namespace clockapp;
struct PreviewPort {
  uint32_t now = 0, readyAt = 0;
  size_t write(const uint8_t*, size_t length) {
    if (!drained()) return 0;
    readyAt = now + static_cast<uint32_t>((length * 1000 + 959) / 960); // 9600 baud, 8N1
    return length;
  }
  bool drained() const { return now >= readyAt; }
};
using Screen = DisplayService<PreviewPort>;
CellFrame frame(const char* top, const char* bottom) {
  CellFrame value; value.setAscii(0, top); value.setAscii(1, bottom); return value;
}
void step(Screen& screen, PreviewPort& port, uint32_t now) { port.now = now; screen.tick(now); }
std::string displayUtf8(const Cell& cell) {
  if (!cell.second()) return std::string(1, static_cast<char>(cell.first()));
  const uint16_t gb = (uint16_t(cell.first()) << 8) | cell.second();
  for (const auto& mapping : encoding::gb2312) {
    if (mapping.gb != gb) continue;
    const auto point = mapping.unicode;
    if (point < 0x800) return {char(0xc0 | (point >> 6)), char(0x80 | (point & 63))};
    return {char(0xe0 | (point >> 12)), char(0x80 | ((point >> 6) & 63)), char(0x80 | (point & 63))};
  }
  return "?";
}
void emitScene(const char* id, const char* name, Motion motion) {
  PreviewPort port; Screen screen(port, motion, 100);
  const bool messageScene = std::string(id) == "message" || std::string(id) == "scroll" || std::string(id) == "confirm";
  const bool rotationScene = std::string(id) == "rotation";
  ContentRotation rotation; rotation.configure({3, 5});
  Environment environment; environment.accept(24.6f, 51.2f, 0);
  tm local{}; local.tm_year = 126; local.tm_mon = 9; local.tm_mday = 21; local.tm_wday = 3;
  local.tm_hour = 14; local.tm_min = 32; local.tm_sec = 10;
  MessageInbox inbox;
  if (messageScene) inbox.receive(std::string(id) == "scroll" ? "请记得喝水，长时间使用电脑后起身活动一下，看看远处放松眼睛。" : "喝杯水，休息一下。");
  bool confirmed = false;
  const bool firstData = std::string(id) == "data";
  const auto original = frame("10-21 14:32:10", firstData ? "T:--.-C RH:--.-%" : "T:24.6C RH:51.2%");
  screen.page(original, 0, 0); screen.begin(2);
  uint32_t start = 0, end = 1800;
  if (std::string(id) != "boot") {
    for (uint32_t now = 0; now <= 2000; now += 20) step(screen, port, now);
    start = 2100; end = 3300;
    if (rotationScene) end = start + 18000;
    else if (messageScene) end = start + (std::string(id) == "scroll" ? 9000 : 3000);
    else if (std::string(id) == "sleep") screen.sleep(start);
    else if (std::string(id) == "wake") {
      screen.sleep(start);
      for (uint32_t now = start; now <= 2700; now += 20) step(screen, port, now);
      start = 2800; end = 4000;
      screen.page(frame("10-21 14:35:02", "T:24.7C RH:51.2%"), 0, start);
      screen.wake(start);
    } else if (firstData) {
      screen.page(frame("10-21 14:32:11", "T:24.6C RH:51.2%"), 0, start, true);
    } else if (!rotationScene) screen.page(frame("10-21 14:32:11", "OUT:18.2C CLOUDY"), 1, start, false, 2);
  }
  std::cout << "{\"id\":" << jsonQuote(id) << ",\"name\":" << jsonQuote(name) << ",\"frames\":[";
  bool first = true;
  for (uint32_t now = start; now <= end; now += 20) {
    if (messageScene || rotationScene) {
      port.now = now;
      if (rotationScene && now == start + 6500) inbox.receive("喝杯水，休息一下。");
      const auto acknowledgeAt = rotationScene ? 8500u : 1000u;
      if ((std::string(id) == "confirm" || rotationScene) && !confirmed && now - start >= acknowledgeAt && screen.pageVisible(inbox.frontId(), now))
        confirmed = inbox.acknowledge(inbox.visibleId(), true, now);
      if (rotationScene) rotation.tick(now, !inbox.count() && !inbox.confirming(now), screen.pageVisible(rotation.key(), now));
      const auto base = rotationScene ? CellFrame(renderContent(rotation.page(), &local, environment, true, true, 0)) : original;
      const auto selected = inbox.view(base, true, now);
      const auto key = selected.key ? selected.key : (rotationScene ? rotation.key() : 0);
      screen.page(selected.frame, key, now, false, 2);
    }
    step(screen, port, now);
    if ((now - start) % 100) continue;
    if (!first) std::cout << ',';
    first = false;
    std::cout << "{\"ms\":" << now - start << ",\"state\":" << int(screen.state()) << ",\"rows\":[";
    for (size_t row = 0; row < 2; ++row) {
      if (row) std::cout << ',';
      std::string text;
      for (const auto& cell : screen.visible().rows[row]) text += displayUtf8(cell);
      std::cout << jsonQuote(text);
    }
    std::cout << "]}";
  }
  std::cout << "]}";
}
int main() {
  std::cout << '{';
  const char* modes[] = {"关闭", "轻量", "标准"};
  for (unsigned mode = 0; mode < 3; ++mode) {
    if (mode) std::cout << ',';
    std::cout << jsonQuote(modes[mode]) << ":[";
    const char* ids[] = {"boot", "wake", "sleep", "switch", "data", "message", "scroll", "confirm", "rotation"};
    const char* names[] = {"开机展开", "唤醒揭示", "睡眠收拢", "内容切换", "首次数据入场", "中文消息入场", "长消息滚动", "消息确认反馈", "轮播与消息打断"};
    for (unsigned i = 0; i < 9; ++i) {
      if (i) std::cout << ',';
      emitScene(ids[i], names[i], static_cast<Motion>(mode));
    }
    std::cout << ']';
  }
  std::cout << "}\n";
}
