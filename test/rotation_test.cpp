#include "clock/rotation.h"
#include "clock/display_service.h"
#include "clock/messages.h"
#include <cassert>
#include <iostream>
#include <string>
using namespace clockapp;
std::string row(const Frame& frame, size_t line) { return std::string(frame.lines[line].data(), 20); }
void settings() {
  RotationSettings settings{15, 120}, decoded;
  const auto record = encodeRotation(settings);
  assert(decodeRotation(record, decoded) && sameRotation(settings, decoded));
  for (size_t i = 0; i < record.size(); ++i) {
    auto corrupt = record; corrupt[i] ^= 1;
    decoded = settings;
    assert(!decodeRotation(corrupt, decoded) && sameRotation(decoded, settings));
  }
  auto bad = encodeRotation({0, 8}); assert(!decodeRotation(bad, decoded));
  bad = encodeRotation({16, 8}); assert(!decodeRotation(bad, decoded));
  bad = encodeRotation({1, 4}); assert(!decodeRotation(bad, decoded));
  bad = encodeRotation({1, 121}); assert(!decodeRotation(bad, decoded));
  assert(parseRotationForm("hold=5&pages=1", decoded) && decoded.pages == 1 && decoded.holdSeconds == 5);
  assert(parseRotationForm("pages=15&hold=120", decoded));
  for (const char* input : {"pages=0&hold=8", "pages=16&hold=8", "pages=1&hold=4", "pages=1&hold=121", "pages=1",
      "pages=1&hold=8&pages=2", "pages=1&hold=8&extra=1", "pages=1&hold=8&", "pages=%00&hold=8", "pages=%XX&hold=8"}) {
    decoded = settings;
    assert(!parseRotationForm(input, decoded) && sameRotation(decoded, settings));
  }
}
void policy() {
  ContentRotation rotation;
  rotation.configure({15, 5});
  assert(rotation.page() == ContentPage::date && rotation.key() == 3);
  rotation.tick(100000, true, false); // not yet on screen: do not consume hold
  rotation.tick(200000, true, true);
  rotation.tick(204999, true, false); // UART update after first visibility does not reset reading time
  assert(rotation.page() == ContentPage::date);
  rotation.tick(205000, true, true);
  assert(rotation.page() == ContentPage::indoor);
  rotation.tick(300000, true, false); assert(rotation.page() == ContentPage::indoor);
  rotation.tick(300100, true, true);
  rotation.tick(301000, false, false); // message/setup/sleep all use this gate
  rotation.tick(400000, false, false); assert(rotation.page() == ContentPage::indoor);
  rotation.tick(400100, true, false); // resume transition must finish first
  rotation.tick(400500, true, true);
  rotation.tick(405499, true, true); assert(rotation.page() == ContentPage::indoor);
  rotation.tick(405500, true, true); assert(rotation.page() == ContentPage::temperature);
  rotation.tick(405501, true, true); rotation.tick(410501, true, true);
  assert(rotation.page() == ContentPage::humidity);
  rotation.tick(410502, true, true); rotation.tick(415502, true, true);
  assert(rotation.page() == ContentPage::date);
  rotation.configure({8, 5}); assert(rotation.page() == ContentPage::humidity && rotation.key() == 6);
  rotation.tick(0xfffffff0u, true, true); rotation.tick(0x00001378u, true, true);
  assert(rotation.page() == ContentPage::humidity); // single selected page stays fixed across rollover
  rotation.configure({5, 5});
  rotation.tick(0xfffffff0u, true, true); rotation.tick(0x00001377u, true, true);
  assert(rotation.page() == ContentPage::date);
  rotation.tick(0x00001378u, true, true); assert(rotation.page() == ContentPage::temperature);
  rotation.configure({0, 8}); assert(rotation.page() == ContentPage::temperature); // invalid edit ignored
}
void pages() {
  tm local{}; local.tm_year = 126; local.tm_mon = 9; local.tm_mday = 21; local.tm_wday = 3;
  local.tm_hour = 14; local.tm_min = 32; local.tm_sec = 10;
  Environment environment; assert(environment.accept(-5.2f, 51.2f, 100));
  const auto date = renderContent(ContentPage::date, &local, environment, true, true, 100);
  assert(row(date, 1) == "2026-10-21 WED      ");
  auto temperature = renderContent(ContentPage::temperature, &local, environment, true, false, 100);
  assert(row(temperature, 1).find("TEMP: -5.2C") == 0 && temperature.lines[0][19] == '!');
  assert(date.lines[0][0] == temperature.lines[0][0]);
  const auto stale = renderContent(ContentPage::humidity, &local, environment, true, true, 15100);
  assert(row(stale, 1).find("HUMIDITY: 51.2%") == 0 && stale.lines[1][19] == '*');
  const auto unknown = renderContent(ContentPage::date, nullptr, environment, true, true, 100);
  assert(row(unknown, 1).find("Date unavailable") == 0 && row(unknown, 1).find("1970") == std::string::npos);
  const auto disabled = renderContent(ContentPage::temperature, &local, environment, false, true, 100);
  assert(row(disabled, 1).find("not enabled") != std::string::npos);
  const auto missing = renderContent(ContentPage::humidity, &local, Environment{}, true, true, 100);
  assert(row(missing, 1).find("--.-%") != std::string::npos);
}
struct Port {
  uint32_t now = 0, busyUntil = 0;
  size_t write(const uint8_t*, size_t size) { if (!drained()) return 0; busyUntil = now + 100; return size; }
  bool drained() const { return now >= busyUntil; }
};
void composition() {
  Port port; DisplayService<Port> screen(port, Motion::standard);
  ContentRotation rotation; rotation.configure({3, 5});
  MessageInbox inbox;
  Environment environment; environment.accept(24.6, 51.2, 0);
  bool confirmed = false;
  uint32_t resumedAt = 0;
  for (uint32_t now = 0; now <= 20000; now += 20) {
    port.now = now;
    if (now == 0) screen.begin(2);
    if (now == 7500) assert(inbox.receive("message interruption") == MessageInbox::Received::added);
    if (now >= 10000 && !confirmed && screen.pageVisible(inbox.frontId(), now)) {
      confirmed = inbox.acknowledge(inbox.visibleId(), true, now);
      assert(confirmed);
    }
    const bool eligible = screen.state() == DisplayService<Port>::State::awake && !inbox.count() && !inbox.confirming(now);
    rotation.tick(now, eligible, screen.pageVisible(rotation.key(), now));
    auto base = CellFrame(renderContent(rotation.page(), nullptr, environment, true, true, now));
    const auto selected = inbox.view(base, screen.state() == DisplayService<Port>::State::awake, now);
    screen.page(selected.frame, selected.key ? selected.key : rotation.key(), now);
    screen.tick(now);
    if (now >= 7500 && !confirmed) assert(rotation.page() == ContentPage::indoor);
    if (confirmed && eligible && screen.pageVisible(rotation.key(), now)) {
      if (!resumedAt) resumedAt = now;
      if (now - resumedAt < 5000) assert(rotation.page() == ContentPage::indoor);
    }
  }
  assert(confirmed && resumedAt && rotation.page() == ContentPage::date);
}
int main() {
  settings(); policy(); pages(); composition();
  std::cout << "PASS: rotation persistence/forms, visible-page dwell, priority pause/resume, rollover, real data rendering and message/display integration\n";
}
