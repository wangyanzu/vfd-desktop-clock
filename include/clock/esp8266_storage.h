#pragma once
#include <LittleFS.h>
#include <cstring>

namespace clockapp {
// One file per versioned settings record. Rename commits the complete record
// after the staging file is closed, keeping the previous file on write failure.
class FlashSettings {
 public:
  bool begin() { return LittleFS.begin(); }
  size_t getBytesLength(const char* key) {
    auto file = LittleFS.open(path(key), "r");
    return file ? file.size() : 0;
  }
  size_t getBytes(const char* key, void* output, size_t size) {
    auto file = LittleFS.open(path(key), "r");
    return file && file.size() == size ? file.read(static_cast<uint8_t*>(output), size) : 0;
  }
  size_t putBytes(const char* key, const void* input, size_t size) {
    constexpr char staging[] = "/clock-new";
    auto file = LittleFS.open(staging, "w");
    if (!file) return 0;
    const auto written = file.write(static_cast<const uint8_t*>(input), size);
    file.close();
    if (written == size && getBytesLength("new") == size && LittleFS.rename(staging, path(key))) return size;
    LittleFS.remove(staging);
    return 0;
  }
  size_t getString(const char* key, char* output, size_t capacity) {
    const size_t size = getBytesLength(key);
    if (!size || size >= capacity || getBytes(key, output, size) != size) return 0;
    output[size] = '\0';
    return size;
  }
  size_t putString(const char* key, const char* value) {
    return putBytes(key, value, std::strlen(value));
  }
 private:
  static String path(const char* key) { return String("/clock-") + key; }
};
}
