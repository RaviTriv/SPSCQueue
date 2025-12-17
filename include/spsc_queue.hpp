#pragma once

#include <array>
#include <cstddef>

namespace spsc {
template <typename T, std::size_t Capacity> class Queue {
public:
  Queue() = default;

  bool push(const T &item) {
    if (full()) {
      return false;
    }
    buffer_[tail_] = item;
    tail_ = (tail_ + 1) % Capacity;
    return true;
  }

  bool pop(T &item) {
    if (empty()) {
      return false;
    }
    item = buffer_[head_];
    head_ = (head_ + 1) % Capacity;
    return true;
  }

  bool empty() const { return head_ == tail_; }
  bool full() const { return (tail_ + 1) % Capacity == head_; }
  static constexpr std::size_t capacity() { return Capacity; };
  std::size_t size() const {
    if (tail_ >= head_) {
      return tail_ - head_;
    }
    return Capacity - head_ + tail_;
  };

private:
  std::array<T, Capacity> buffer_;
  std::size_t head_ = 0;
  std::size_t tail_ = 0;
};
} // namespace spsc
