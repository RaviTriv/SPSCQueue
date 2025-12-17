#include <spsc_queue.hpp>

int main() {
  spsc::Queue<int, 5> q;
  for (int i = 0; i < 5; i++) {
    q.push(i);
  }

  while (!q.empty()) {
    int item;
    q.pop(item);
    printf("%d\n", item);
  }

  return 0;
}
