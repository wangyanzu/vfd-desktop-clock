#pragma once
#include <cstdint>
#include <cstring>

namespace clockapp::w4home {
// One NVS blob: no cross-key transaction can separate token and its owner.
struct Binding {
  uint32_t version = 1;
  char mac[13]{};
  char server[256]{};
  char token[65]{};
  uint8_t reserved[2]{};
};
static_assert(sizeof(Binding) == 340, "Persistent binding layout changed");
inline bool hex(const char *s, size_t digits, size_t capacity) {
  if (capacity <= digits || s[digits] != '\0') return false;
  for (size_t i = 0; i < digits; ++i)
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
  return true;
}
inline bool valid(const Binding &b) {
  return b.version == 1 && hex(b.mac, 12, sizeof(b.mac)) &&
    hex(b.token, 64, sizeof(b.token)) &&
    std::memchr(b.server, 0, sizeof(b.server)) &&
    (std::strncmp(b.server, "https://", 8) == 0 || std::strncmp(b.server, "http://", 7) == 0);
}
inline bool matches(const Binding &b, const char *mac, const char *server) {
  return valid(b) && std::strcmp(b.mac, mac) == 0 && std::strcmp(b.server, server) == 0;
}
}
