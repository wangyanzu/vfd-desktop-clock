#pragma once
#include "clock/core.h"
#include "clock/gb2312_table.h"
#include <string_view>

namespace clockapp {
struct DisplayText {
  static constexpr size_t capacity = 128;
  std::array<Cell, capacity> cells{};
  uint16_t length = 0, substitutions = 0;
};
// Strict UTF-8; control bytes never reach the display protocol. Unsupported
// codepoints become one '?' cell, including a complete four-byte emoji.
inline bool encodeMessage(std::string_view utf8, DisplayText& destination) {
  if (utf8.empty() || utf8.size() > 256) return false;
  DisplayText next;
  for (size_t i = 0; i < utf8.size();) {
    uint32_t point = static_cast<uint8_t>(utf8[i++]);
    unsigned remaining = 0; uint32_t minimum = 0;
    if (point < 0x80) {
      if (point == '\n' || point == '\r' || point == '\t') point = ' ';
      else if (point < 0x20 || point == 0x7f) return false;
    } else {
      if (point >= 0xc2 && point <= 0xdf) { point &= 0x1f; remaining = 1; minimum = 0x80; }
      else if (point >= 0xe0 && point <= 0xef) { point &= 0x0f; remaining = 2; minimum = 0x800; }
      else if (point >= 0xf0 && point <= 0xf4) { point &= 0x07; remaining = 3; minimum = 0x10000; }
      else return false;
      if (utf8.size() - i < remaining) return false;
      while (remaining--) {
        const auto byte = static_cast<uint8_t>(utf8[i++]);
        if ((byte & 0xc0) != 0x80) return false;
        point = (point << 6) | (byte & 0x3f);
      }
      if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) return false;
    }
    if (next.length == DisplayText::capacity) return false;
    Cell cell;
    if (point < 0x80) cell = Cell::ascii(static_cast<char>(point));
    else {
      size_t left = 0, right = sizeof(encoding::gb2312) / sizeof(encoding::gb2312[0]);
      while (left < right) {
        const auto middle = left + (right - left) / 2;
        if (encoding::gb2312[middle].unicode < point) left = middle + 1;
        else right = middle;
      }
      if (left < sizeof(encoding::gb2312) / sizeof(encoding::gb2312[0]) && encoding::gb2312[left].unicode == point) {
        const auto bytes = encoding::gb2312[left].gb;
        Cell::gb2312(bytes >> 8, bytes & 0xff, cell);
      } else { cell = Cell::ascii('?'); ++next.substitutions; }
    }
    next.cells[next.length++] = cell;
  }
  bool nonSpace = false;
  for (size_t i = 0; i < next.length; ++i) if (next.cells[i] != Cell{}) nonSpace = true;
  if (!nonSpace) return false;
  destination = next;
  return true;
}

class MessageInbox {
 public:
  static constexpr size_t capacity = 8;
  static constexpr uint32_t confirmationMs = 1000;
  static constexpr uint32_t initialHoldMs = 1500, scrollStepMs = 400, endHoldMs = 1200;
  enum class Received { added, duplicate, invalid, full };
  struct View { CellFrame frame; uint32_t key = 0, id = 0; bool message = false; };
  Received receive(std::string_view payload) {
    DisplayText text;
    if (!encodeMessage(payload, text)) { ++rejected_; return Received::invalid; }
    for (size_t i = 0; i < count_; ++i) {
      const auto& existing = queue_[(head_ + i) % capacity];
      if (existing.bytes == payload.size() && std::memcmp(existing.utf8.data(), payload.data(), payload.size()) == 0) {
        ++duplicates_; return Received::duplicate;
      }
    }
    if (count_ == capacity) { ++rejected_; return Received::full; }
    auto& message = queue_[(head_ + count_) % capacity];
    message.text = text; message.utf8.fill(0);
    std::memcpy(message.utf8.data(), payload.data(), payload.size());
    message.bytes = static_cast<uint16_t>(payload.size()); message.id = nextId_++;
    if (nextId_ < 16) nextId_ = 16; // IDs 0..15 are reserved for system pages
    ++count_;
    return Received::added;
  }
  View view(const CellFrame& base, bool eligible, uint32_t now) {
    View result; result.frame = base;
    if (!eligible) { activeId_ = 0; visibleId_ = 0; feedback_ = false; return result; }
    if (feedback_ && !elapsed(now, confirmedAt_, confirmationMs)) {
      result.frame.setAscii(1, "MESSAGE CONFIRMED"); result.key = 2; visibleId_ = 0;
      return result;
    }
    feedback_ = false;
    if (!count_) { activeId_ = visibleId_ = 0; return result; }
    const auto& current = queue_[head_];
    if (activeId_ != current.id) { activeId_ = current.id; enteredAt_ = now; }
    size_t offset = 0;
    if (current.text.length > 20) {
      const size_t span = current.text.length - 20;
      const uint32_t cycle = initialHoldMs + static_cast<uint32_t>(span) * scrollStepMs + endHoldMs;
      const auto position = static_cast<uint32_t>(now - enteredAt_) % cycle;
      if (position >= initialHoldMs) offset = (position - initialHoldMs) / scrollStepMs;
      if (offset > span) offset = span;
    }
    result.frame.rows[1] = {};
    for (size_t column = 0; column < 20 && offset + column < current.text.length; ++column)
      result.frame.rows[1][column] = current.text.cells[offset + column];
    result.frame.rows[0][17] = Cell::ascii('M');
    result.frame.rows[0][18] = Cell::ascii(static_cast<char>('0' + count_));
    result.key = result.id = current.id; result.message = true; visibleId_ = current.id;
    return result;
  }
  bool acknowledge(uint32_t id, bool eligible, uint32_t now) {
    // A stale browser response/button event must never clear the next message.
    if (!eligible || !id || !count_ || queue_[head_].id != id || visibleId_ != id) return false;
    queue_[head_] = {}; head_ = (head_ + 1) % capacity; --count_;
    activeId_ = visibleId_ = 0; feedback_ = true; confirmedAt_ = now;
    return true;
  }
  bool confirming(uint32_t now) const { return feedback_ && !elapsed(now, confirmedAt_, confirmationMs); }
  uint32_t visibleId() const { return visibleId_; }
  uint32_t frontId() const { return count_ ? queue_[head_].id : 0; }
  size_t count() const { return count_; }
  uint32_t rejected() const { return rejected_; }
  uint32_t duplicates() const { return duplicates_; }
 private:
  struct Message { DisplayText text; std::array<char, 257> utf8{}; uint16_t bytes = 0; uint32_t id = 0; };
  std::array<Message, capacity> queue_{};
  size_t head_ = 0, count_ = 0;
  uint32_t nextId_ = 16, activeId_ = 0, visibleId_ = 0, enteredAt_ = 0, confirmedAt_ = 0;
  uint32_t rejected_ = 0, duplicates_ = 0;
  bool feedback_ = false;
};

inline bool parseMessageAck(std::string_view body, uint32_t& destination) {
  if (body.substr(0, 3) != "id=" || body.size() < 4 || body.size() > 13) return false;
  uint64_t id = 0;
  for (char c : body.substr(3)) {
    if (c < '0' || c > '9') return false;
    id = id * 10 + static_cast<unsigned>(c - '0');
    if (id > UINT32_MAX) return false;
  }
  if (id < 16) return false;
  destination = static_cast<uint32_t>(id);
  return true;
}
}
