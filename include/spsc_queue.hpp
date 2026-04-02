#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <system_error>
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
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
  static constexpr std::size_t kMask = Capacity - 1;

public:
  Queue() = default;

  bool push(const T &item) {
    std::size_t t = tail_.load(std::memory_order_relaxed);
    std::size_t h = head_.load(std::memory_order_acquire);

    if (((t + 1) & kMask) == h) {
      return false;
    }
    buffer_[t] = item;
    tail_.store((t + 1) & kMask, std::memory_order_release);
    return true;
  }

  bool pop(T &item) {
    std::size_t t = tail_.load(std::memory_order_acquire);
    std::size_t h = head_.load(std::memory_order_relaxed);
    if (t == h) {
      return false;
    }
    item = buffer_[h];
    head_.store((h + 1) & kMask, std::memory_order_release);
    return true;
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
  alignas(kCacheLineSize) std::atomic<std::size_t> head_{0};
  alignas(kCacheLineSize) std::atomic<std::size_t> tail_{0};
  alignas(kCacheLineSize) std::array<T, Capacity> buffer_;
};

template <typename T, std::size_t Capacity> struct SharedQueueStorage {
  alignas(kCacheLineSize) std::atomic<std::size_t> head{0};
  alignas(kCacheLineSize) std::atomic<std::size_t> tail{0};
  alignas(kCacheLineSize) std::array<T, Capacity> buffer;
};

template <typename T, std::size_t Capacity>
class Queue<T, Capacity, MemoryType::Shared> {
  static_assert(Capacity > 0, "Capacity must be positive");
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
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
      throw std::system_error(errno, std::system_category(), "shm_open failed!");
    }

    if (is_creator) {
      if (ftruncate(shm_fd_, total_size) == -1) {
        close(shm_fd_);
        shm_unlink(shm_name_.c_str());
        throw std::system_error(errno, std::system_category(), "ftruncate failed!");
      }
    }

    mapped_region_ = mmap(nullptr, total_size, PROT_READ | PROT_WRITE,
                          MAP_SHARED, shm_fd_, 0);
    if (mapped_region_ == MAP_FAILED) {
      close(shm_fd_);
      if (is_creator) shm_unlink(shm_name_.c_str());
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
    std::size_t t = storage_->tail.load(std::memory_order_relaxed);
    std::size_t h = storage_->head.load(std::memory_order_acquire);

    if (((t + 1) & kMask) == h) {
      return false;
    }
    storage_->buffer[t] = item;
    storage_->tail.store((t + 1) & kMask, std::memory_order_release);
    return true;
  }

  bool pop(T &item) {
    std::size_t t = storage_->tail.load(std::memory_order_acquire);
    std::size_t h = storage_->head.load(std::memory_order_relaxed);

    if (t == h) {
      return false;
    }
    item = storage_->buffer[h];
    storage_->head.store((h + 1) & kMask, std::memory_order_release);
    return true;
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
  int shm_fd_ = -1;
  void *mapped_region_ = nullptr;
  std::size_t mapped_size_ = 0;
  std::string shm_name_;
  SharedQueueStorage<T, Capacity> *storage_ = nullptr;
};

} // namespace spsc
