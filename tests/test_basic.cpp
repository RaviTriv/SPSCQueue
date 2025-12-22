#include <gtest/gtest.h>
#include <spsc_queue.hpp>

class SPSCQueueTest : public ::testing::Test {
protected:
    spsc::Queue<int, 8> queue;
};

TEST_F(SPSCQueueTest, StartsEmpty) {
    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.full());
    EXPECT_EQ(queue.size(), 0);
}

TEST_F(SPSCQueueTest, PushIncrementsSize) {
    EXPECT_TRUE(queue.push(42));
    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(queue.size(), 1);
}

TEST_F(SPSCQueueTest, PopReturnsCorrectValue) {
    queue.push(42);
    int val = 0;
    EXPECT_TRUE(queue.pop(val));
    EXPECT_EQ(val, 42);
    EXPECT_TRUE(queue.empty());
}

TEST_F(SPSCQueueTest, PopFromEmptyReturnsFalse) {
    int val = 0;
    EXPECT_FALSE(queue.pop(val));
}

TEST_F(SPSCQueueTest, FIFOOrdering) {
    queue.push(1);
    queue.push(2);
    queue.push(3);

    int val = 0;
    queue.pop(val);
    EXPECT_EQ(val, 1);
    queue.pop(val);
    EXPECT_EQ(val, 2);
    queue.pop(val);
    EXPECT_EQ(val, 3);
}

TEST_F(SPSCQueueTest, FullCondition) {
    for (int i = 0; i < 7; i++) {
        EXPECT_TRUE(queue.push(i));
    }
    EXPECT_TRUE(queue.full());
    EXPECT_FALSE(queue.push(999));
}

TEST_F(SPSCQueueTest, WrapAround) {
    for (int i = 0; i < 5; i++) {
        queue.push(i);
    }
    int val = 0;
    for (int i = 0; i < 5; i++) {
        queue.pop(val);
    }

    for (int i = 100; i < 107; i++) {
        EXPECT_TRUE(queue.push(i));
    }

    for (int i = 100; i < 107; i++) {
        EXPECT_TRUE(queue.pop(val));
        EXPECT_EQ(val, i);
    }
    EXPECT_TRUE(queue.empty());
}

TEST_F(SPSCQueueTest, InterleavedPushPop) {
    int val = 0;
    for (int round = 0; round < 100; round++) {
        queue.push(round);
        queue.pop(val);
        EXPECT_EQ(val, round);
    }
    EXPECT_TRUE(queue.empty());
}

TEST_F(SPSCQueueTest, SizeIsAccurate) {
    EXPECT_EQ(queue.size(), 0);
    queue.push(1);
    EXPECT_EQ(queue.size(), 1);
    queue.push(2);
    EXPECT_EQ(queue.size(), 2);

    int val = 0;
    queue.pop(val);
    EXPECT_EQ(queue.size(), 1);
    queue.pop(val);
    EXPECT_EQ(queue.size(), 0);
}

TEST_F(SPSCQueueTest, CapacityIsCorrect) {
    EXPECT_EQ(queue.capacity(), 8);

    using LargeQueue = spsc::Queue<int, 1024>;
    EXPECT_EQ(LargeQueue::capacity(), 1024);
}
