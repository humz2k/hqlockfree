#include <hqlockfree/spsc_queue.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

using namespace hqlockfree;

TEST(SPSCQueueBasic, PushPopSingleThread) {
    spsc_queue<int> q(1 /* lines */, 8 /* elems */);
    EXPECT_EQ(q.size(), 0u);

    q.push(5);
    EXPECT_EQ(q.size(), 1u);

    int out = 0;
    ASSERT_TRUE(q.pop(out));
    EXPECT_EQ(out, 5);
    EXPECT_EQ(q.size(), 0u);

    EXPECT_FALSE(q.pop(out)); // now empty
}

TEST(SPSCQueueBasic, PopEmpty) {
    spsc_queue<int> q(1, 4);
    int dummy;
    EXPECT_FALSE(q.pop(dummy));
}

TEST(SPSCQueueCorrectness, WrapAround) {
    spsc_queue<int> q(1, 8); // very small capacity
    const std::size_t rounds = q.capacity() * 5;

    int out;
    for (std::size_t i = 0; i < rounds; ++i) {
        q.push(static_cast<int>(i));
        ASSERT_TRUE(q.pop(out));
        EXPECT_EQ(out, static_cast<int>(i));
    }
}

TEST(SPSCQueueCorrectness, FullQueueBlocksUntilConsumerFreesSpace) {
    spsc_queue<int> q(1, 4);
    const std::size_t max_fill = q.capacity() - 1;

    // Pre-fill queue
    for (std::size_t i = 0; i < max_fill; ++i)
        q.push(static_cast<int>(i));

    std::atomic<bool> producer_done{false};
    std::thread producer([&] {
        q.push(999); // must spin-wait
        producer_done.store(true, std::memory_order_relaxed);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(producer_done.load()) << "Producer should still be blocked";

    int tmp;
    ASSERT_TRUE(q.pop(tmp)); // consumer frees one slot
    EXPECT_EQ(tmp, 0);

    producer.join();
    EXPECT_TRUE(producer_done.load());

    for (size_t i = 1; i < max_fill; ++i) {
        ASSERT_TRUE(q.pop(tmp));
        EXPECT_EQ(tmp, static_cast<int>(i));
    }

    ASSERT_TRUE(q.pop(tmp));
    EXPECT_EQ(tmp, 999);
}

TEST(SPSCQueueConcurrency, HighThroughputStress) {
    constexpr std::size_t total_items = 200'000;

    spsc_queue<std::uint64_t> q(32 /* lines */);

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < total_items; ++i)
            q.push(i);
    });

    std::vector<std::uint64_t> received;
    received.reserve(total_items);

    std::thread consumer([&] {
        std::uint64_t val;
        while (received.size() < total_items) {
            if (q.pop(val))
                received.push_back(val);
        }
    });

    producer.join();
    consumer.join();

    // validate sequence integrity
    ASSERT_EQ(received.size(), total_items);
    for (std::size_t i = 0; i < total_items; ++i) {
        EXPECT_EQ(received[i], i);
    }
}

TEST(SPSCQueueProperties, SizeAndCapacity) {
    spsc_queue<int> q(2 /* lines */);
    EXPECT_EQ(q.capacity() & (q.capacity() - 1U), 0u) // power-of-two?
        << "Capacity should be pow-2 for default policy";

    for (int i = 0; i < 10; ++i) {
        q.push(i);
        EXPECT_EQ(q.size(), static_cast<std::size_t>(i + 1));
    }
    int dummy;
    for (int i = 9; i >= 0; --i) {
        q.pop(dummy);
        EXPECT_EQ(q.size(), static_cast<std::size_t>(i));
    }
}