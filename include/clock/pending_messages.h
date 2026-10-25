#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace clockapp {
struct IncomingMessage {
  std::array<char,257> payload{};
  uint16_t length=0;
  uint8_t level=1;
  uint32_t generation=0;
};
// Caller provides synchronization; no allocations or parsing inside the lock.
class PendingMessages {
 public:
  static constexpr size_t capacity=4;
  enum class Push { added, evicted, rejected, duplicate };
  Push push(const IncomingMessage& message) {
    if (!message.length || message.length>256 || message.level<1 || message.level>3) return Push::rejected;
    for (size_t i=0;i<count_;++i) {
      const auto& old=queue_[i];
      if (old.generation==message.generation && old.level==message.level && old.length==message.length &&
          std::memcmp(old.payload.data(),message.payload.data(),message.length)==0) return Push::duplicate;
    }
    bool evicted=false;
    if (count_==capacity) {
      size_t victim=capacity; uint8_t lowest=4;
      for (size_t i=0;i<count_;++i) {
        if (queue_[i].level<3 && queue_[i].level<=message.level && queue_[i].level<lowest) {
          lowest=queue_[i].level; victim=i;
        }
      }
      if (victim==capacity) return Push::rejected;
      for (size_t i=victim;i+1<count_;++i) queue_[i]=queue_[i+1];
      --count_; evicted=true;
    }
    queue_[count_++]=message;
    return evicted ? Push::evicted : Push::added;
  }
  bool pop(IncomingMessage& message) {
    if (!count_) return false;
    message=queue_[0];
    for (size_t i=0;i+1<count_;++i) queue_[i]=queue_[i+1];
    queue_[--count_]={}; return true;
  }
 private:
  std::array<IncomingMessage,capacity> queue_{};
  size_t count_=0;
};
}
