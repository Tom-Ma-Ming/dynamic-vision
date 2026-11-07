#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>
#include <vector>

#include "vision/core/BoundedQueue.hpp"

using namespace std::chrono_literals;
using vision::BoundedQueue;
using vision::QueueMode;

TEST(BoundedQueue, RejectsZeroCapacity) {
  EXPECT_THROW(BoundedQueue<int>(0, QueueMode::Blocking), std::invalid_argument);
}

TEST(BoundedQueue, TryPopOnOpenEmptyQueueReturnsNullopt) {
  BoundedQueue<int> queue(1, QueueMode::Blocking);
  EXPECT_FALSE(queue.tryPop().has_value());
}

TEST(BoundedQueue, LatestCapacityOneKeepsNewest) {
  BoundedQueue<int> queue(1, QueueMode::Latest);
  EXPECT_TRUE(queue.push(1));
  EXPECT_TRUE(queue.push(2));
  EXPECT_TRUE(queue.push(3));

  EXPECT_EQ(queue.replaced(), 2u);
  EXPECT_EQ(queue.dropped(), 2u);
  EXPECT_EQ(queue.peak(), 1u);
  EXPECT_EQ(queue.pushed(), 3u);

  auto value = queue.tryPop();
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 3);
  EXPECT_FALSE(queue.tryPop().has_value());
  EXPECT_EQ(queue.popped(), 1u);
}

TEST(BoundedQueue, LatestCapacityTwoKeepsNewestTwo) {
  BoundedQueue<int> queue(2, QueueMode::Latest);
  EXPECT_TRUE(queue.push(1));
  EXPECT_TRUE(queue.push(2));
  EXPECT_TRUE(queue.push(3));

  EXPECT_EQ(queue.replaced(), 1u);
  EXPECT_EQ(queue.peak(), 2u);

  auto first = queue.tryPop();
  auto second = queue.tryPop();
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*first, 2);
  EXPECT_EQ(*second, 3);
}

TEST(BoundedQueue, LatestSlowConsumerAccountsForEveryFrame) {
  // T07: capacity 2 and a consumer that deliberately sleeps while the producer
  // runs. Every pushed item is either processed or replaced; the queue never
  // exceeds its capacity.
  BoundedQueue<int> queue(2, QueueMode::Latest);
  std::atomic<bool> started{false};
  std::atomic<std::size_t> processed{0};

  std::thread consumer([&] {
    started = true;
    std::this_thread::sleep_for(50ms);
    while (auto value = queue.pop()) {
      ++processed;
    }
  });

  while (!started.load()) std::this_thread::yield();
  for (int i = 0; i < 100; ++i) {
    ASSERT_TRUE(queue.push(i));
  }
  queue.close();
  consumer.join();

  EXPECT_EQ(processed.load() + queue.replaced(), 100u);
  EXPECT_GT(queue.replaced(), 0u);
  EXPECT_LE(queue.peak(), 2u);
  EXPECT_EQ(queue.capacity(), 2u);
}

TEST(BoundedQueue, BlockingModeAppliesBackpressureWithoutDrops) {
  BoundedQueue<int> queue(2, QueueMode::Blocking);
  std::atomic<bool> producerOk{true};

  std::thread producer([&] {
    for (int i = 0; i < 1000; ++i) {
      if (!queue.push(i)) {
        producerOk = false;
        break;
      }
    }
    queue.close();
  });

  std::vector<int> received;
  while (auto value = queue.pop()) {
    received.push_back(*value);
  }
  producer.join();

  EXPECT_TRUE(producerOk.load());
  ASSERT_EQ(received.size(), 1000u);
  for (int i = 0; i < 1000; ++i) {
    EXPECT_EQ(received[static_cast<std::size_t>(i)], i);
  }
  EXPECT_EQ(queue.replaced(), 0u);
  EXPECT_EQ(queue.peak(), 2u);
}

TEST(BoundedQueue, CloseWakesBlockedConsumer) {
  BoundedQueue<int> queue(1, QueueMode::Blocking);
  std::atomic<bool> exited{false};
  std::atomic<bool> receivedValue{false};

  std::thread consumer([&] {
    auto value = queue.pop();
    receivedValue = value.has_value();
    exited = true;
  });

  std::this_thread::sleep_for(20ms);
  queue.close();
  consumer.join();

  EXPECT_TRUE(exited.load());
  EXPECT_FALSE(receivedValue.load());
}

TEST(BoundedQueue, ConsumerFailureUnblocksProducerAndJoins) {
  BoundedQueue<int> queue(1, QueueMode::Blocking);
  std::atomic<bool> producerExited{false};
  std::atomic<bool> pushRejected{false};

  std::thread producer([&] {
    for (int i = 0; i < 1000; ++i) {
      if (!queue.push(i)) {
        pushRejected = true;
        break;
      }
    }
    producerExited = true;
  });

  auto first = queue.pop();
  ASSERT_TRUE(first.has_value());
  queue.close();
  producer.join();

  EXPECT_TRUE(producerExited.load());
  EXPECT_TRUE(pushRejected.load());
  EXPECT_FALSE(queue.push(999));
}

TEST(BoundedQueue, CloseDrainsPendingItemsInOrder) {
  BoundedQueue<int> queue(2, QueueMode::Blocking);
  ASSERT_TRUE(queue.push(1));
  ASSERT_TRUE(queue.push(2));
  queue.close();

  auto first = queue.pop();
  auto second = queue.pop();
  auto third = queue.pop();

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*first, 1);
  EXPECT_EQ(*second, 2);
  EXPECT_FALSE(third.has_value());
  EXPECT_EQ(queue.popped(), 2u);
}

TEST(BoundedQueue, PushAfterCloseIsRejected) {
  BoundedQueue<int> queue(1, QueueMode::Latest);
  queue.close();

  EXPECT_TRUE(queue.closed());
  EXPECT_FALSE(queue.push(1));
  EXPECT_EQ(queue.pushed(), 0u);
  EXPECT_FALSE(queue.tryPop().has_value());
}

TEST(BoundedQueue, PeakTracksMaximumPendingSize) {
  BoundedQueue<int> queue(3, QueueMode::Blocking);
  ASSERT_TRUE(queue.push(1));
  ASSERT_TRUE(queue.push(2));
  ASSERT_TRUE(queue.tryPop().has_value());
  ASSERT_TRUE(queue.push(3));
  ASSERT_TRUE(queue.push(4));

  EXPECT_EQ(queue.peak(), 3u);
  EXPECT_EQ(queue.size(), 3u);
}
