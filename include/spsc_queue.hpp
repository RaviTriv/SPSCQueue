#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <system_error>
#include <type_traits>
#include <unistd.h>

namespace spsc {

inline constexpr std::size_t kCacheLineSize = 64;

enum class MemoryType { Local, Shared };
enum class OpenMode { Create, Open, OpenOrCreate };

struct SharedMemoryConfig {
  std::string name;
  OpenMode mode = OpenMode::OpenOrCreate;
};

template <typename T, std::size_t Capacity,
          MemoryType Memory = MemoryType::Local>
class Queue {
  static_assert(Capacity > 0, "Capacity must be positive");
  static_assert((Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of 2");
  static constexpr std::size_t kMask = Capacity - 1;

public:
  Queue() = default;

  bool push(const T &item) {
    const std::size_t t = tail_.load(std::memory_order_relaxed);
    const std::size_t next = (t + 1) & kMask;

    if (next == cached_head_) {
      cached_head_ = head_.load(std::memory_order_acquire);
      if (next == cached_head_) {
        return false;
      }
    }
    buffer_[t] = item;
    tail_.store(next, std::memory_order_release);
    return true;
  }

  bool pop(T &item) {
    const std::size_t h = head_.load(std::memory_order_relaxed);

    if (h == cached_tail_) {
      cached_tail_ = tail_.load(std::memory_order_acquire);
      if (h == cached_tail_) {
        return false;
      }
    }
    item = buffer_[h];
    head_.store((h + 1) & kMask, std::memory_order_release);
    return true;
  }

  std::size_t push_n(const T *items, std::size_t count) {
    if (count == 0)
      return 0;

    const std::size_t t = tail_.load(std::memory_order_relaxed);

    std::size_t available = (cached_head_ - t - 1) & kMask;
    if (available < count) {
      cached_head_ = head_.load(std::memory_order_acquire);
      available = (cached_head_ - t - 1) & kMask;
    }

    count = std::min(count, available);
    if (count == 0)
      return 0;

    const std::size_t first_chunk = std::min(count, Capacity - t);
    copy_into_buffer(buffer_.data() + t, items, first_chunk);
    if (first_chunk < count) {
      copy_into_buffer(buffer_.data(), items + first_chunk,
                       count - first_chunk);
    }

    tail_.store((t + count) & kMask, std::memory_order_release);
    return count;
  }

  std::size_t pop_n(T *out, std::size_t count) {
    if (count == 0)
      return 0;

    const std::size_t h = head_.load(std::memory_order_relaxed);

    std::size_t available = (cached_tail_ - h) & kMask;
    if (available < count) {
      cached_tail_ = tail_.load(std::memory_order_acquire);
      available = (cached_tail_ - h) & kMask;
    }

    count = std::min(count, available);
    if (count == 0)
      return 0;

    const std::size_t first_chunk = std::min(count, Capacity - h);
    copy_from_buffer(out, buffer_.data() + h, first_chunk);
    if (first_chunk < count) {
      copy_from_buffer(out + first_chunk, buffer_.data(), count - first_chunk);
    }

    head_.store((h + count) & kMask, std::memory_order_release);
    return count;
  }

  bool empty() const { return head_.load() == tail_.load(); }
  bool full() const { return ((tail_.load() + 1) & kMask) == head_.load(); }
  static constexpr std::size_t capacity() { return Capacity; }
  static constexpr std::size_t usable_capacity() { return Capacity - 1; }
  std::size_t size() const {
    std::size_t h = head_.load();
    std::size_t t = tail_.load();
    return (t - h) & kMask;
  }

private:
  static void copy_into_buffer(T *dst, const T *src, std::size_t n) {
    if constexpr (std::is_trivially_copyable_v<T>) {
      std::memcpy(dst, src, n * sizeof(T));
    } else {
      std::copy(src, src + n, dst);
    }
  }

  static void copy_from_buffer(T *dst, const T *src, std::size_t n) {
    if constexpr (std::is_trivially_copyable_v<T>) {
      std::memcpy(dst, src, n * sizeof(T));
    } else {
      std::copy(src, src + n, dst);
    }
  }

  alignas(kCacheLineSize) std::atomic<std::size_t> head_{0};
  std::size_t cached_tail_{0};
  alignas(kCacheLineSize) std::atomic<std::size_t> tail_{0};
  std::size_t cached_head_{0};
  alignas(kCacheLineSize) std::array<T, Capacity> buffer_;
};

template <typename T, std::size_t Capacity> struct SharedQueueStorage {
  alignas(kCacheLineSize) std::atomic<std::size_t> head{0};
  std::size_t cached_tail{0};
  alignas(kCacheLineSize) std::atomic<std::size_t> tail{0};
  std::size_t cached_head{0};
  alignas(kCacheLineSize) std::array<T, Capacity> buffer;
};

template <typename T, std::size_t Capacity>
class Queue<T, Capacity, MemoryType::Shared> {
  static_assert(Capacity > 0, "Capacity must be positive");
  static_assert((Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of 2");
  static constexpr std::size_t kMask = Capacity - 1;

public:
  explicit Queue(const SharedMemoryConfig &config) : shm_name_(config.name) {
    constexpr std::size_t total_size = sizeof(SharedQueueStorage<T, Capacity>);

    bool is_creator = false;

    switch (config.mode) {
    case OpenMode::Create:
      shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
      is_creator = true;
      break;
    case OpenMode::Open:
      shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0600);
      break;
    case OpenMode::OpenOrCreate:
      shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
      if (shm_fd_ != -1) {
        is_creator = true;
      } else if (errno == EEXIST) {
        shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0600);
      }
      break;
    }
    if (shm_fd_ == -1) {
      throw std::system_error(errno, std::system_category(),
                              "shm_open failed!");
    }

    if (is_creator) {
      if (ftruncate(shm_fd_, total_size) == -1) {
        close(shm_fd_);
        shm_unlink(shm_name_.c_str());
        throw std::system_error(errno, std::system_category(),
                                "ftruncate failed!");
      }
    }

    mapped_region_ = mmap(nullptr, total_size, PROT_READ | PROT_WRITE,
                          MAP_SHARED, shm_fd_, 0);
    if (mapped_region_ == MAP_FAILED) {
      close(shm_fd_);
      if (is_creator)
        shm_unlink(shm_name_.c_str());
      throw std::system_error(errno, std::system_category(), "mmap failed!!");
    }
    mapped_size_ = total_size;

    storage_ = static_cast<SharedQueueStorage<T, Capacity> *>(mapped_region_);

    if (is_creator) {
      storage_->head.store(0, std::memory_order_relaxed);
      storage_->tail.store(0, std::memory_order_relaxed);
    }
  }

  ~Queue() {
    if (mapped_region_ && mapped_region_ != MAP_FAILED) {
      munmap(mapped_region_, mapped_size_);
    }
    if (shm_fd_ != -1) {
      close(shm_fd_);
    }
  }

  Queue(const Queue &) = delete;
  Queue &operator=(const Queue &) = delete;

  Queue(Queue &&other) noexcept
      : shm_fd_(other.shm_fd_), mapped_region_(other.mapped_region_),
        mapped_size_(other.mapped_size_), shm_name_(std::move(other.shm_name_)),
        storage_(other.storage_) {
    other.shm_fd_ = -1;
    other.mapped_region_ = nullptr;
    other.storage_ = nullptr;
  }

  Queue &operator=(Queue &&other) noexcept {
    if (this != &other) {
      if (mapped_region_ && mapped_region_ != MAP_FAILED) {
        munmap(mapped_region_, mapped_size_);
      }
      if (shm_fd_ != -1) {
        close(shm_fd_);
      }
      shm_fd_ = other.shm_fd_;
      mapped_region_ = other.mapped_region_;
      mapped_size_ = other.mapped_size_;
      shm_name_ = std::move(other.shm_name_);
      storage_ = other.storage_;
      other.shm_fd_ = -1;
      other.mapped_region_ = nullptr;
      other.storage_ = nullptr;
    }
    return *this;
  }

  bool push(const T &item) {
    const std::size_t t = storage_->tail.load(std::memory_order_relaxed);
    const std::size_t next = (t + 1) & kMask;

    if (next == storage_->cached_head) {
      storage_->cached_head = storage_->head.load(std::memory_order_acquire);
      if (next == storage_->cached_head) {
        return false;
      }
    }
    storage_->buffer[t] = item;
    storage_->tail.store(next, std::memory_order_release);
    return true;
  }

  bool pop(T &item) {
    const std::size_t h = storage_->head.load(std::memory_order_relaxed);

    if (h == storage_->cached_tail) {
      storage_->cached_tail = storage_->tail.load(std::memory_order_acquire);
      if (h == storage_->cached_tail) {
        return false;
      }
    }
    item = storage_->buffer[h];
    storage_->head.store((h + 1) & kMask, std::memory_order_release);
    return true;
  }

  std::size_t push_n(const T *items, std::size_t count) {
    if (count == 0)
      return 0;

    const std::size_t t = storage_->tail.load(std::memory_order_relaxed);

    std::size_t available = (storage_->cached_head - t - 1) & kMask;
    if (available < count) {
      storage_->cached_head = storage_->head.load(std::memory_order_acquire);
      available = (storage_->cached_head - t - 1) & kMask;
    }

    count = std::min(count, available);
    if (count == 0)
      return 0;

    const std::size_t first_chunk = std::min(count, Capacity - t);
    copy_into_buffer(storage_->buffer.data() + t, items, first_chunk);
    if (first_chunk < count) {
      copy_into_buffer(storage_->buffer.data(), items + first_chunk,
                       count - first_chunk);
    }

    storage_->tail.store((t + count) & kMask, std::memory_order_release);
    return count;
  }

  std::size_t pop_n(T *out, std::size_t count) {
    if (count == 0)
      return 0;

    const std::size_t h = storage_->head.load(std::memory_order_relaxed);

    std::size_t available = (storage_->cached_tail - h) & kMask;
    if (available < count) {
      storage_->cached_tail = storage_->tail.load(std::memory_order_acquire);
      available = (storage_->cached_tail - h) & kMask;
    }

    count = std::min(count, available);
    if (count == 0)
      return 0;

    const std::size_t first_chunk = std::min(count, Capacity - h);
    copy_from_buffer(out, storage_->buffer.data() + h, first_chunk);
    if (first_chunk < count) {
      copy_from_buffer(out + first_chunk, storage_->buffer.data(),
                       count - first_chunk);
    }

    storage_->head.store((h + count) & kMask, std::memory_order_release);
    return count;
  }

  bool empty() const { return storage_->head.load() == storage_->tail.load(); }
  bool full() const {
    return ((storage_->tail.load() + 1) & kMask) == storage_->head.load();
  }
  static constexpr std::size_t capacity() { return Capacity; }
  static constexpr std::size_t usable_capacity() { return Capacity - 1; }

  std::size_t size() const {
    std::size_t h = storage_->head.load();
    std::size_t t = storage_->tail.load();
    return (t - h) & kMask;
  }

  const std::string &name() const { return shm_name_; }

  static void unlink(const std::string &name) { shm_unlink(name.c_str()); }

private:
  static void copy_into_buffer(T *dst, const T *src, std::size_t n) {
    if constexpr (std::is_trivially_copyable_v<T>) {
      std::memcpy(dst, src, n * sizeof(T));
    } else {
      std::copy(src, src + n, dst);
    }
  }

  static void copy_from_buffer(T *dst, const T *src, std::size_t n) {
    if constexpr (std::is_trivially_copyable_v<T>) {
      std::memcpy(dst, src, n * sizeof(T));
    } else {
      std::copy(src, src + n, dst);
    }
  }

  int shm_fd_ = -1;
  void *mapped_region_ = nullptr;
  std::size_t mapped_size_ = 0;
  std::string shm_name_;
  SharedQueueStorage<T, Capacity> *storage_ = nullptr;
};

} // namespace spsc
