#pragma once
#include "clock/core.h"
#include "clock/settings.h"

namespace clockapp {
enum class ContentPage : uint8_t { date, indoor, temperature, humidity };
inline constexpr uint8_t contentPageBit(ContentPage page) { return 1u << static_cast<unsigned>(page); }
inline const char* contentPageName(ContentPage page) {
  switch (page) {
    case ContentPage::date: return "date";
    case ContentPage::indoor: return "indoor";
    case ContentPage::temperature: return "temperature";
    case ContentPage::humidity: return "humidity";
  }
  return "date";
}
struct RotationSettings {
  uint8_t pages = contentPageBit(ContentPage::date) | contentPageBit(ContentPage::indoor);
  uint16_t holdSeconds = 8;
};
inline bool validRotationSettings(const RotationSettings& settings) {
  return settings.pages >= 1 && settings.pages <= 15 && settings.holdSeconds >= 5 && settings.holdSeconds <= 120;
}
inline bool sameRotation(const RotationSettings& a, const RotationSettings& b) {
  return a.pages == b.pages && a.holdSeconds == b.holdSeconds;
}
using RotationRecord = std::array<uint8_t, 11>;
inline RotationRecord encodeRotation(const RotationSettings& settings) {
  RotationRecord record{'R', 'T', 'P', 1, settings.pages,
    static_cast<uint8_t>(settings.holdSeconds), static_cast<uint8_t>(settings.holdSeconds >> 8)};
  const auto checksum = crc32(record.data(), 7);
  for (unsigned i = 0; i < 4; ++i) record[7 + i] = static_cast<uint8_t>(checksum >> (8 * i));
  return record;
}
inline bool decodeRotation(const RotationRecord& record, RotationSettings& destination) {
  if (std::memcmp(record.data(), "RTP\1", 4) != 0) return false;
  uint32_t checksum = 0;
  for (unsigned i = 0; i < 4; ++i) checksum |= uint32_t(record[7 + i]) << (8 * i);
  if (checksum != crc32(record.data(), 7)) return false;
  const RotationSettings next{record[4], static_cast<uint16_t>(record[5] | (uint16_t(record[6]) << 8))};
  if (!validRotationSettings(next)) return false;
  destination = next;
  return true;
}
inline bool parseRotationForm(std::string_view body, RotationSettings& destination) {
  if (body.empty() || body.size() > 64) return false;
  int pages = 0, hold = 0;
  unsigned seen = 0;
  while (!body.empty()) {
    const auto end = body.find('&');
    const auto part = body.substr(0, end);
    const auto equal = part.find('=');
    if (equal == std::string_view::npos) return false;
    std::string key, value;
    if (!decodeFormValue(part.substr(0, equal), key) || !decodeFormValue(part.substr(equal + 1), value)) return false;
    if (key == "pages" && !(seen & 1)) {
      if (!parseInteger(value, 1, 15, pages)) return false;
      seen |= 1;
    } else if (key == "hold" && !(seen & 2)) {
      if (!parseInteger(value, 5, 120, hold)) return false;
      seen |= 2;
    } else return false;
    if (end == std::string_view::npos) break;
    body.remove_prefix(end + 1);
    if (body.empty()) return false;
  }
  if (seen != 3) return false;
  destination = {static_cast<uint8_t>(pages), static_cast<uint16_t>(hold)};
  return true;
}

class ContentRotation {
 public:
  void configure(const RotationSettings& settings) {
    if (!validRotationSettings(settings) || sameRotation(settings_, settings)) return;
    settings_ = settings;
    current_ = firstPage();
    timing_ = false;
  }
  void tick(uint32_t now, bool eligible, bool pageVisible) {
    // Pause does not consume reading time. Resume the same page for a full hold.
    if (!eligible) { timing_ = false; return; }
    if (!timing_) {
      if (!pageVisible) return;
      shownAt_ = now;
      timing_ = true;
    }
    if (!elapsed(now, shownAt_, uint32_t(settings_.holdSeconds) * 1000)) return;
    for (unsigned step = 1; step <= 4; ++step) {
      const auto candidate = static_cast<ContentPage>((static_cast<unsigned>(current_) + step) % 4);
      if (settings_.pages & contentPageBit(candidate)) {
        current_ = candidate;
        timing_ = false;
        return;
      }
    }
  }
  ContentPage page() const { return current_; }
  // Keys 0..2 belong to ordinary legacy/setup/confirmation pages, >=16 to messages.
  uint32_t key() const { return 3 + static_cast<unsigned>(current_); }
 private:
  ContentPage firstPage() const {
    for (unsigned i = 0; i < 4; ++i)
      if (settings_.pages & (1u << i)) return static_cast<ContentPage>(i);
    return ContentPage::date;
  }
  RotationSettings settings_{};
  ContentPage current_ = ContentPage::date;
  uint32_t shownAt_ = 0;
  bool timing_ = false;
};

inline Frame renderContent(ContentPage page, const tm* localTime, const Environment& environment,
                           bool sensorEnabled, bool connected, uint32_t now) {
  auto frame = render(localTime, environment, sensorEnabled, connected, now);
  char text[48];
  if (page == ContentPage::indoor) return frame;
  if (page == ContentPage::date) {
    if (!localTime) frame.set(1, "Date unavailable");
    else {
      const char* weekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
      std::strftime(text, sizeof(text), "%Y-%m-%d", localTime);
      frame.set(1, text);
      const auto weekday = localTime->tm_wday;
      const char* label = weekday >= 0 && weekday < 7 ? weekdays[weekday] : "---";
      std::memcpy(frame.lines[1].data() + 11, label, 3);
    }
    return frame;
  }
  if (!sensorEnabled) frame.set(1, "AHT10 not enabled");
  else if (!environment.valid) frame.set(1, page == ContentPage::temperature ? "TEMP: --.-C" : "HUMIDITY: --.-%");
  else {
    if (page == ContentPage::temperature) std::snprintf(text, sizeof(text), "TEMP: %.1fC", environment.temperature);
    else std::snprintf(text, sizeof(text), "HUMIDITY: %.1f%%", environment.humidity);
    frame.set(1, text);
    if (!environment.fresh(now)) frame.lines[1][19] = '*';
  }
  return frame;
}
}
