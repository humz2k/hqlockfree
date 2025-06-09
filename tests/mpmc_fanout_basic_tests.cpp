#include <hqlockfree/mpmc_fanout.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <unordered_set>
#include <vector>

using namespace hqlockfree;

TEST(MPMCFanoutBasic, SingleProducerSingleConsumer) {
    mpmc_fanout<int> q(1 /* lines */, 8 /* elems */);

    auto sub = q.subscribe();
    q.push(7);

    int out = 0;
    ASSERT_TRUE(sub->pop(out));
    EXPECT_EQ(out, 7);
    EXPECT_FALSE(sub->pop(out)); // queue now empty

    EXPECT_LE(q.size(), 1u);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(q.size(), 0u);
}

TEST(MPMCFanoutBasic, LateSubscription) {
    mpmc_fanout<int> q(1, 8);

    // Produce some data before subscribing.
    for (int i = 0; i < 5; ++i)
        q.push(i);

    auto sub = q.subscribe(); // should start at read_head
    int out;
    EXPECT_FALSE(sub->pop(out)); // nothing yet

    q.push(42);
    ASSERT_TRUE(sub->pop(out));
    EXPECT_EQ(out, 42);
}

TEST(MPMCFanoutCorrectness, WrapAround) {
    mpmc_fanout<int> q(1, 8); // capacity very small
    auto sub = q.subscribe();

    const std::size_t rounds = q.capacity() * 4;
    for (std::size_t i = 0; i < rounds; ++i) {
        q.push(static_cast<int>(i));
        int val;
        ASSERT_TRUE(sub->pop(val));
        EXPECT_EQ(val, static_cast<int>(i));
    }
}

TEST(MPMCFanoutCorrectness, ProducerBlocksUntilConsumerAdvances) {
    mpmc_fanout<int> q(1, 4);
    auto sub = q.subscribe();

    const std::size_t max_fill = q.capacity() - 1;
    for (std::size_t i = 0; i < max_fill; ++i)
        q.push(static_cast<int>(i));

    std::atomic<bool> done{false};
    std::thread prod([&] {
        q.push(777);
        done = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(done.load()) << "producer should be spinning, queue full";

    int dummy;
    ASSERT_TRUE(sub->pop(dummy)); // free one slot
    EXPECT_EQ(dummy, 0);

    prod.join();
    EXPECT_TRUE(done.load()) << "producer should be done";

    for (std::size_t i = 1; i < max_fill; ++i) {
        ASSERT_TRUE(sub->pop(dummy));
        EXPECT_EQ(dummy, static_cast<int>(i));
    }

    ASSERT_TRUE(sub->pop(dummy));
    EXPECT_EQ(dummy, 777);
}

TEST(MPMCFanoutCorrectness, UnsubscribeAndCleanup) {
    mpmc_fanout<int> q(2, 16);

    auto sub1 = q.subscribe();
    auto sub2 = q.subscribe();

    // Advance sub1 but not sub2
    for (int i = 0; i < 10; ++i) {
        q.push(i);
        int tmp;
        ASSERT_TRUE(sub1->pop(tmp));
        EXPECT_EQ(tmp, i);
    }

    EXPECT_EQ(q.size(), 10);

    int tmp;
    ASSERT_TRUE(sub2->pop(tmp));
    EXPECT_EQ(tmp, 0);

    // Wait a short moment for the daemon to update min_tail
    std::this_thread::sleep_for(std::chrono::milliseconds(25));

    EXPECT_EQ(q.size(), 9);

    // Unsubscribe the slow consumer
    sub2->unsubscribe();

    // Wait a short moment for the daemon to prune & update min_tail
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    EXPECT_EQ(q.size(), 0);
}