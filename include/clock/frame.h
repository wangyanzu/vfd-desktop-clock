#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace clockapp {
// ASCII layout retained for existing pages. Animated/display frames use cells.
struct Frame {
  static constexpr size_t columns = 20;
  using Line = std::array<char, columns + 1>;
  std::array<Line, 2> lines{};
  Frame() { for (auto& line : lines) { line.fill(' '); line[columns] = '\0'; } }
  void set(size_t row, const char* text) {
    if (row >= lines.size()) return;
    lines[row].fill(' ');
    const auto length = std::strlen(text);
    std::memcpy(lines[row].data(), text, length < columns ? length : columns);
    lines[row][columns] = '\0';
  }
};

class Cell {
 public:
  static Cell ascii(char character) {
    Cell cell;
    const auto value = static_cast<uint8_t>(character);
    cell.first_ = value >= 0x20 && value <= 0x7e ? value : '?';
    return cell;
  }
  static bool gb2312(uint8_t first, uint8_t second, Cell& destination) {
    if (first < 0xa1 || first > 0xfe || second < 0xa1 || second > 0xfe) return false;
    destination.first_ = first; destination.second_ = second;
    return true;
  }
  uint8_t first() const { return first_; }
  uint8_t second() const { return second_; }
  bool operator==(const Cell& other) const { return first_ == other.first_ && second_ == other.second_; }
  bool operator!=(const Cell& other) const { return !(*this == other); }
 private:
  uint8_t first_ = ' ', second_ = 0;
};

struct CellFrame {
  static constexpr size_t columns = 20;
  using Row = std::array<Cell, columns>;
  std::array<Row, 2> rows{};
  CellFrame() = default;
  explicit CellFrame(const Frame& ascii) {
    for (size_t row = 0; row < 2; ++row)
      for (size_t column = 0; column < columns; ++column)
        rows[row][column] = Cell::ascii(ascii.lines[row][column]);
  }
  void setAscii(size_t row, const char* text) {
    if (row >= 2) return;
    Frame frame;
    frame.set(row, text);
    rows[row] = CellFrame(frame).rows[row];
  }
  // Input is already GB2312, not UTF-8. One glyph occupies one physical cell.
  // Reject invalid/truncated/overlong input transactionally.
  bool setEncoded(size_t row, std::string_view text) {
    if (row >= 2) return false;
    Row candidate{};
    size_t column = 0;
    for (size_t i = 0; i < text.size(); ++i) {
      if (column == columns) return false;
      const auto first = static_cast<uint8_t>(text[i]);
      if (first >= 0x20 && first <= 0x7e) candidate[column++] = Cell::ascii(static_cast<char>(first));
      else {
        if (++i >= text.size() || !Cell::gb2312(first, static_cast<uint8_t>(text[i]), candidate[column])) return false;
        ++column;
      }
    }
    rows[row] = candidate;
    return true;
  }
  bool operator==(const CellFrame& other) const { return rows == other.rows; }
  bool operator!=(const CellFrame& other) const { return !(*this == other); }
};
}
