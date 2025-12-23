#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace spsc {
template <typename T, std::size_t Capacity> class Queue {
public:
  Queue() = default;

  bool push(const T &item) {
    std::size_t t = tail_.load(std::memory_order_relaxed);
    std::size_t h = head_.load(std::memory_order_acquire);

    if ((t + 1) % Capacity == h) {
      return false;
    }
    buffer_[t] = item;
    tail_.store((t + 1) % Capacity, std::memory_order_release);
    return true;
  }

  bool pop(T &item) {
    std::size_t t = tail_.load(std::memory_order_acquire);
    std::size_t h = head_.load(std::memory_order_relaxed);
    if (t == h) {
      return false;
    }
    item = buffer_[h];
    head_.store((h + 1) % Capacity, std::memory_order_release);
    return true;
  }

  bool empty() const { return head_.load() == tail_.load(); }
  bool full() const { return (tail_.load() + 1) % Capacity == head_.load(); }
  static constexpr std::size_t capacity() { return Capacity; };
  std::size_t size() const {
    std::size_t h = head_.load();
    std::size_t t = tail_.load();
    if (t >= h) {
      return t - h;
    }
    return Capacity - h + t;
  };

private:
  std::array<T, Capacity> buffer_;
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
};
} // namespace spsc
