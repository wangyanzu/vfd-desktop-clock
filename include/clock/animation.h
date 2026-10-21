#pragma once
#include "clock/core.h"

namespace clockapp {
enum class Motion : uint8_t { off, reduced, standard };
enum class Effect { replace, reveal, revealFields, centerReveal, pushLeft, sleep };

class Animator {
 public:
  // visible_ advances only when a frame was accepted by the transport. An
  // interrupted effect therefore starts from what was actually submitted.
  void start(Effect effect, const CellFrame& target, uint32_t now, uint32_t duration, uint8_t rows = 3) {
    source_ = visible_; target_ = target; effect_ = effect;
    startedAt_ = now; duration_ = duration; rows_ = rows;
  }
  void target(const CellFrame& target) { target_ = target; }
  void committed(const CellFrame& frame) { visible_ = frame; }
  const CellFrame& visible() const { return visible_; }
  bool active(uint32_t now) const { return duration_ && effect_ != Effect::replace && !elapsed(now, startedAt_, duration_); }
  CellFrame sample(uint32_t now) const {
    if (!active(now)) return target_;
    CellFrame frame = target_;
    const uint32_t age = static_cast<uint32_t>(now - startedAt_);
    const size_t count = static_cast<size_t>(uint64_t(age) * 20 / duration_);
    for (size_t row = 0; row < 2; ++row) {
      if (!(rows_ & (1u << row))) continue;
      for (size_t column = 0; column < 20; ++column) {
        switch (effect_) {
          case Effect::pushLeft:
            frame.rows[row][column] = column + count < 20 ? source_.rows[row][column + count] : target_.rows[row][column + count - 20];
            break;
          case Effect::reveal:
            frame.rows[row][column] = column < count ? target_.rows[row][column] : source_.rows[row][column];
            break;
          case Effect::revealFields: {
            // Existing data pages separate label+value fields with spaces.
            // Replace an entire field together, never splice old/new digits.
            size_t end = column;
            while (end < 20 && target_.rows[row][end] != Cell{}) ++end;
            const bool covered = target_.rows[row][column] == Cell{} ? column < count : end <= count;
            frame.rows[row][column] = covered ? target_.rows[row][column] : source_.rows[row][column];
            break;
          }
          case Effect::centerReveal: {
            const size_t left = (20 - count) / 2, right = left + count;
            frame.rows[row][column] = column >= left && column < right ? target_.rows[row][column] : source_.rows[row][column];
            break;
          }
          case Effect::sleep: {
            const size_t total = static_cast<size_t>(uint64_t(age) * 40 / duration_);
            const size_t removed = row == 1 ? (total < 20 ? total : 20) : (total > 20 ? total - 20 : 0);
            frame.rows[row][column] = column < removed / 2 || column >= 20 - (removed + 1) / 2 ? Cell{} : source_.rows[row][column];
            break;
          }
          case Effect::replace: break;
        }
      }
    }
    return frame;
  }
 private:
  CellFrame source_, target_, visible_;
  Effect effect_ = Effect::replace;
  uint32_t startedAt_ = 0, duration_ = 0;
  uint8_t rows_ = 3;
};
}
