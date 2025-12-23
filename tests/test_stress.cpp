#include <gtest/gtest.h>
#include <spsc_queue.hpp>

#include <atomic>
#include <thread>

class SPSCStressTest : public ::testing::Test {};

TEST_F(SPSCStressTest, VOLUME) {
  constexpr int NUM_OPS = 1000000;
  spsc::Queue<int, 1024> queue;

  std::thread producer([&] {
    for (int i = 0; i < NUM_OPS; i++) {
      while (!queue.push(i)) {
        std::this_thread::yield();
      }
    }
  });

  std::thread consumer([&] {
    for (int i = 0; i < NUM_OPS; i++) {
      int val = -1;
      while (!queue.pop(val)) {
        std::this_thread::yield();
      }
      ASSERT_EQ(val, i) << "Ordering violated at index " << i << "!!! :<";
    }
  });

  producer.join();
  consumer.join();
  EXPECT_TRUE(queue.empty());
}

TEST_F(SPSCStressTest, LargeObject) {
  struct LargeObject {
    int id;
    std::array<char, 256> data;

    bool operator==(const LargeObject &other) const {
      return id == other.id && data == other.data;
    }
  };

  constexpr int NUM_OPS = 100000;
  spsc::Queue<LargeObject, 128> queue;

  std::thread producer([&] {
    for (int i = 0; i < NUM_OPS; i++) {
      LargeObject obj;
      obj.id = i;
      obj.data.fill(static_cast<char>(i % 128));
      while (!queue.push(obj)) {
        std::this_thread::yield();
      }
    }
  });

  std::thread consumer([&] {
    for (int i = 0; i < NUM_OPS; i++) {
      LargeObject obj;
      while (!queue.pop(obj)) {
        std::this_thread::yield();
      }
      ASSERT_EQ(obj.id, i);
      for (char c : obj.data) {
        ASSERT_EQ(c, static_cast<char>(i % 128));
      }
    }
  });

  producer.join();
  consumer.join();
}

TEST_F(SPSCStressTest, Burst) {
  constexpr int BURST_SIZE = 500;
  constexpr int NUM_BURSTS = 1000;
  spsc::Queue<int, 1024> queue;

  std::atomic<bool> done{false};

  std::thread producer([&] {
    int counter = 0;
    for (int burst = 0; burst < NUM_BURSTS; burst++) {
      for (int i = 0; i < BURST_SIZE; i++) {
        while (!queue.push(counter)) {
          std::this_thread::yield();
        }
        counter++;
      }
    }
    done.store(true, std::memory_order_release);
  });

  std::thread consumer([&] {
    int expected = 0;
    while (!done.load(std::memory_order_acquire) || !queue.empty()) {
      int val;
      if (queue.pop(val)) {
        ASSERT_EQ(val, expected);
        expected++;
      } else {
        std::this_thread::yield();
      }
    }
    EXPECT_EQ(expected, BURST_SIZE * NUM_BURSTS);
  });

  producer.join();
  consumer.join();
}
