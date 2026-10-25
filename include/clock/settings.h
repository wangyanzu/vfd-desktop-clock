#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace clockapp {
struct Settings {
  char ssid[33]{};
  char password[65]{};
  char ntp[64] = "ntp.aliyun.com";
  int16_t utcOffsetMinutes = 480;
  uint8_t brightness = 2;
  uint16_t sleepMinutes = 60;
};

inline bool printable(std::string_view value) {
  for (unsigned char c : value) if (c < 0x20 || c == 0x7f) return false;
  return true;
}
inline bool validUtf8(std::string_view value) {
  for (size_t i = 0; i < value.size();) {
    const auto lead = static_cast<uint8_t>(value[i++]);
    if (lead < 0x80) continue;
    unsigned count = 0;
    uint32_t code = 0, minimum = 0;
    if (lead >= 0xc2 && lead <= 0xdf) { count = 1; code = lead & 0x1f; minimum = 0x80; }
    else if (lead >= 0xe0 && lead <= 0xef) { count = 2; code = lead & 0x0f; minimum = 0x800; }
    else if (lead >= 0xf0 && lead <= 0xf4) { count = 3; code = lead & 0x07; minimum = 0x10000; }
    else return false;
    if (value.size() - i < count) return false;
    while (count--) {
      const auto next = static_cast<uint8_t>(value[i++]);
      if ((next & 0xc0) != 0x80) return false;
      code = (code << 6) | (next & 0x3f);
    }
    if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
  }
  return true;
}
inline bool validHost(std::string_view host) {
  if (host.empty() || host.size() > 63 || host.front() == '.' || host.back() == '.') return false;
  bool labelStart = true;
  char previous = 0;
  for (char c : host) {
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (c == '.') {
      if (labelStart || previous == '-') return false;
      labelStart = true;
    } else {
      if (!alnum && c != '-') return false;
      if (labelStart && c == '-') return false;
      labelStart = false;
    }
    previous = c;
  }
  return previous != '-';
}
inline bool validPassword(std::string_view password) {
  if (password.empty()) return true; // explicitly selected open network
  if (password.size() == 64) {
    for (char c : password)
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    return true;
  }
  if (password.size() < 8 || password.size() > 63) return false;
  for (unsigned char c : password) if (c < 0x20 || c > 0x7e) return false;
  return true;
}
inline bool validSleepMinutes(unsigned value) { return value==0 || value==15 || value==30 || value==60 || value==120; }
inline bool validSettings(const Settings& s) {
  // Check termination before treating persisted bytes as strings.
  if (!std::memchr(s.ssid, 0, sizeof(s.ssid)) || !std::memchr(s.password, 0, sizeof(s.password)) ||
      !std::memchr(s.ntp, 0, sizeof(s.ntp))) return false;
  return printable(s.ssid) && validUtf8(s.ssid) && validPassword(s.password) && validHost(s.ntp) &&
         validSleepMinutes(s.sleepMinutes) && s.brightness >= 1 && s.brightness <= 4 && s.utcOffsetMinutes >= -720 && s.utcOffsetMinutes <= 840;
}
inline bool sameNetwork(const Settings& a, const Settings& b) {
  return std::strcmp(a.ssid, b.ssid) == 0 && std::strcmp(a.password, b.password) == 0;
}
inline std::string timezoneForOffset(int minutes) {
  // POSIX TZ uses the opposite sign to the displayed UTC offset.
  char value[20];
  const int magnitude = minutes < 0 ? -minutes : minutes;
  std::snprintf(value, sizeof(value), "UTC%c%d:%02d", minutes < 0 ? '+' : '-', magnitude / 60, magnitude % 60);
  return value;
}

// Explicit, versioned wire format: never persist a C++ struct/padding.
using LegacySettingsRecord = std::array<uint8_t, 173>;
using SettingsRecord = std::array<uint8_t, 175>;
inline uint32_t crc32(const uint8_t* bytes, size_t length) {
  uint32_t crc = 0xffffffff;
  for (size_t i = 0; i < length; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
  }
  return ~crc;
}
inline SettingsRecord encodeSettings(const Settings& settings) {
  SettingsRecord out{};
  const uint8_t magic[] = {'V', 'F', 'C', 2};
  std::memcpy(out.data(), magic, 4);
  std::memcpy(out.data() + 4, settings.ssid, 33);
  std::memcpy(out.data() + 37, settings.password, 65);
  std::memcpy(out.data() + 102, settings.ntp, 64);
  const auto offset = static_cast<uint16_t>(settings.utcOffsetMinutes);
  out[166] = offset & 0xff;
  out[167] = offset >> 8;
  out[168] = settings.brightness;
  out[169] = uint8_t(settings.sleepMinutes); out[170] = uint8_t(settings.sleepMinutes >> 8);
  const uint32_t checksum = crc32(out.data(), 171);
  for (unsigned i = 0; i < 4; ++i) out[171 + i] = (checksum >> (i * 8)) & 0xff;
  return out;
}
template<size_t N>
inline bool decodeSettings(const std::array<uint8_t,N>& record, Settings& destination) {
  if (N != 173 && N != 175) return false;
  if (record[0] != 'V' || record[1] != 'F' || record[2] != 'C' || record[3] != (N==173?1:2)) return false;
  uint32_t expected = 0;
  for (unsigned i = 0; i < 4; ++i) expected |= uint32_t(record[N-4 + i]) << (i * 8);
  if (expected != crc32(record.data(), N-4)) return false;
  Settings candidate{};
  std::memcpy(candidate.ssid, record.data() + 4, 33);
  std::memcpy(candidate.password, record.data() + 37, 65);
  std::memcpy(candidate.ntp, record.data() + 102, 64);
  const uint16_t offset = uint16_t(record[166]) | (uint16_t(record[167]) << 8);
  candidate.utcOffsetMinutes = static_cast<int16_t>(offset < 0x8000 ? int(offset) : int(offset) - 65536);
  candidate.brightness = record[168];
  if constexpr (N==175) candidate.sleepMinutes = uint16_t(record[169]) | uint16_t(record[170]) << 8;
  if (!validSettings(candidate)) return false;
  destination = candidate;
  return true;
}

inline bool parseInteger(std::string_view value, int minimum, int maximum, int& output) {
  if (value.empty() || value.size() > 6) return false;
  size_t i = value[0] == '-' ? 1 : 0;
  if (i == value.size()) return false;
  int result = 0;
  for (; i < value.size(); ++i) {
    if (value[i] < '0' || value[i] > '9') return false;
    result = result * 10 + value[i] - '0';
  }
  if (value[0] == '-') result = -result;
  if (result < minimum || result > maximum) return false;
  output = result;
  return true;
}

struct SettingsInput {
  std::string ssid, password, passwordMode, brightness, offset, ntp;
  std::string sleepMinutes{};
};
// Validation is transactional. A failed edit never changes destination.
inline const char* applySettings(const Settings& current, const SettingsInput& input, Settings& destination) {
  if (input.ssid.empty() || input.ssid.size() > 32 || !printable(input.ssid) || !validUtf8(input.ssid)) return "Wi-Fi 名称须为 1–32 字节有效 UTF-8 文本，且不含控制字符。";
  Settings next = current;
  std::memset(next.ssid, 0, sizeof(next.ssid));
  std::memcpy(next.ssid, input.ssid.data(), input.ssid.size());
  if (input.passwordMode == "keep") {
    if (input.ssid != current.ssid || !input.password.empty()) return "更换网络时，请填写密码或明确选择开放网络。";
  } else if (input.passwordMode == "replace") {
    if (input.password.empty() || !validPassword(input.password)) return "Wi-Fi 密码须为 8–63 个 ASCII 字符，或 64 位十六进制密钥。";
    std::memset(next.password, 0, sizeof(next.password));
    std::memcpy(next.password, input.password.data(), input.password.size());
  } else if (input.passwordMode == "open") {
    if (!input.password.empty()) return "开放网络模式无需填写密码。";
    std::memset(next.password, 0, sizeof(next.password));
  } else return "请选择有效的密码操作。";
  int brightness = 0, offset = 0;
  if (!parseInteger(input.brightness, 1, 4, brightness)) return "亮度须为 1–4 档。";
  if (!parseInteger(input.offset, -720, 840, offset)) return "UTC 偏移须为 -720 到 840 分钟。";
  if (!validHost(input.ntp)) return "校时服务器须为有效主机名或 IPv4 地址，不含 http://。";
  std::memset(next.ntp, 0, sizeof(next.ntp));
  std::memcpy(next.ntp, input.ntp.data(), input.ntp.size());
  if (!input.sleepMinutes.empty()) {
    int minutes=0;
    if (!parseInteger(input.sleepMinutes,0,120,minutes) || !validSleepMinutes(unsigned(minutes))) return "请选择有效的自动休眠时间。";
    next.sleepMinutes = uint16_t(minutes);
  }
  next.brightness = static_cast<uint8_t>(brightness);
  next.utcOffsetMinutes = static_cast<int16_t>(offset);
  destination = next;
  return nullptr;
}

inline std::string jsonQuote(std::string_view value) {
  std::string out = "\"";
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
    else if (c < 0x20) {
      char escape[7]; std::snprintf(escape, sizeof(escape), "\\u%04x", c); out += escape;
    } else out += static_cast<char>(c);
  }
  return out + '"';
}

inline bool decodeFormValue(std::string_view input, std::string& output) {
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  output.clear();
  for (size_t i = 0; i < input.size(); ++i) {
    char c = input[i];
    if (c == '+') c = ' ';
    else if (c == '%') {
      if (i + 2 >= input.size()) return false;
      const int high = hex(input[i + 1]), low = hex(input[i + 2]);
      if (high < 0 || low < 0) return false;
      c = static_cast<char>((high << 4) | low); i += 2;
    }
    if (c == '\0') return false;
    output += c;
  }
  return true;
}
inline bool parseSettingsForm(std::string_view body, SettingsInput& output) {
  if (body.empty() || body.size() > 1024) return false;
  SettingsInput candidate;
  unsigned seen = 0;
  while (!body.empty()) {
    const size_t end = body.find('&');
    const auto part = body.substr(0, end);
    const size_t equal = part.find('=');
    if (equal == std::string_view::npos) return false;
    std::string key, value;
    if (!decodeFormValue(part.substr(0, equal), key) || !decodeFormValue(part.substr(equal + 1), value)) return false;
    const char* names[] = {"ssid", "password", "password_mode", "brightness", "offset", "ntp", "sleep"};
    std::string* fields[] = {&candidate.ssid, &candidate.password, &candidate.passwordMode,
                             &candidate.brightness, &candidate.offset, &candidate.ntp, &candidate.sleepMinutes};
    unsigned index = 0;
    for (; index < 7 && key != names[index]; ++index) {}
    if (index == 7 || (seen & (1u << index))) return false;
    *fields[index] = value; seen |= 1u << index;
    if (end == std::string_view::npos) break;
    body.remove_prefix(end + 1);
    if (body.empty()) return false;
  }
  if ((seen & 63) != 63) return false;
  output = candidate;
  return true;
}
}
