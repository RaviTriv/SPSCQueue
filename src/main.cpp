#include <cassert>
#include <cstdio>
#include <spsc_queue.hpp>
#include <thread>

int main() {
  constexpr int NUM_ITEMS = 100000000;
  spsc::Queue<int, 1024> q;

  std::thread producer([&q]() {
    for (int i = 0; i < NUM_ITEMS; i++) {
      while (!q.push(i)) {
      }
    }
  });

  std::thread consumer([&q]() {
    int expected = 0;
    while (expected < NUM_ITEMS) {
      int val;
      if (q.pop(val)) {
        if (val != expected) {
          printf("ERROR: Expected %d but got %d\n", expected, val);
          printf("Not synced correctly\n");
          exit(1);
        }
        expected++;
      }
    }
  });

  producer.join();
  consumer.join();

  assert(q.empty());
  printf("Transferred %d items successfully.\n", NUM_ITEMS);
  return 0;
}