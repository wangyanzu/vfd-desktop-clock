#pragma once
#include "clock/frame.h"

namespace clockapp {
// Commands confirmed by M202MD28AA AN-C-2409A; raw UART on the user's modified
// hardware. No HID framing, ROM write, font registration or software reset.
// Sink::write must return the complete accepted byte count, or 0 if busy.
template <class Sink> class Vfd {
 public:
  explicit Vfd(Sink& sink) : sink_(sink) {}
  bool begin(uint8_t brightness = 2) {
    if (brightness < 1 || brightness > 4) return false;
    const uint8_t bytes[] = {
      0x1f,0x28,0x61,0x40,0x01, // display power on
      0x0c, 0x1f,0x01,          // clear, overwrite
      0x1f,0x43,0x00,           // cursor off
      0x1f,0x45,0x00,           // blink off
      0x1f,0x72,0x00,           // reverse off
      0x1f,0x28,0x67,0x02,0x01, // two-byte glyphs enabled
      0x1f,0x28,0x67,0x03,0x02, // GB2312
      0x1f,0x58,brightness
    };
    invalid_ = true;
    return send(bytes, sizeof(bytes));
  }
  bool setBrightness(uint8_t level) {
    if (level < 1 || level > 4) return false;
    const uint8_t bytes[] = {0x1f, 0x58, level};
    return send(bytes, sizeof(bytes));
  }
  bool setPower(bool on) {
    const uint8_t bytes[] = {0x1f,0x28,0x61,0x40,static_cast<uint8_t>(on ? 1 : 0)};
    if (!send(bytes, sizeof(bytes))) return false;
    invalid_ = true;
    return true;
  }
  bool show(const Frame& frame) { return show(CellFrame(frame)); }
  bool show(const CellFrame& frame) {
    std::array<uint8_t, 88> bytes{};
    size_t length = 0;
    for (size_t row = 0; row < 2; ++row) {
      if (!invalid_ && frame.rows[row] == previous_.rows[row]) continue;
      bytes[length++] = 0x1f; bytes[length++] = 0x24;
      bytes[length++] = 1; bytes[length++] = static_cast<uint8_t>(row + 1);
      for (const auto& cell : frame.rows[row]) {
        bytes[length++] = cell.first();
        if (cell.second()) bytes[length++] = cell.second();
      }
    }
    if (length && !send(bytes.data(), length)) return false;
    previous_ = frame; invalid_ = false;
    return true;
  }
 private:
  bool send(const uint8_t* bytes, size_t length) { return sink_.write(bytes, length) == length; }
  Sink& sink_;
  CellFrame previous_;
  bool invalid_ = true;
};
}
