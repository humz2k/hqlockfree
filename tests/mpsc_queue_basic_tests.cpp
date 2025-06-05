// mpsc_queue_basic_tests.cpp
#include <hqlockfree/mpsc_queue.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <unordered_set>
#include <vector>

using namespace hqlockfree;

/* --------------------------------------------------------------------------
 *  Helpers for type-trait tests
 * --------------------------------------------------------------------------*/
struct MoveOnly {
    int v;
    explicit MoveOnly(int vv = 0) : v(vv) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&& other) noexcept : v(other.v) {}
    MoveOnly& operator=(MoveOnly&& other) noexcept {
        v = other.v;
        return *this;
    }
    bool operator==(const MoveOnly& rhs) const { return v == rhs.v; }
};

/* --------------------------------------------------------------------------
 *  1. Basic semantics
 * --------------------------------------------------------------------------*/
TEST(MPSCQueueBasic, PushPop) {
    mpsc_queue<int> q(/*min_cache_lines=*/1, /*min_elements=*/8);
    EXPECT_EQ(q.size(), 0u);
    q.push(42);
    int out = 0;
    ASSERT_TRUE(q.pop(out));
    EXPECT_EQ(out, 42);
    EXPECT_FALSE(q.pop(out)); // now empty
    EXPECT_EQ(q.size(), 0u);
}

TEST(MPSCQueueBasic, PopEmpty) {
    mpsc_queue<int> q(1, 4);
    EXPECT_EQ(q.size(), 0u);
    int dummy;
    EXPECT_FALSE(q.pop(dummy));
    EXPECT_EQ(q.size(), 0u);
}

/* --------------------------------------------------------------------------
 *  2. Size & capacity bookkeeping
 * --------------------------------------------------------------------------*/
TEST(MPSCQueueProperties, SizeAndCapacityPow2) {
    const size_t min_cache_lines = 3; // deliberately not a power-of-two
    const size_t min_elements = 7;
    mpsc_queue<int> q(min_cache_lines, min_elements); // default = pow2

    const size_t cap = q.capacity();
    EXPECT_GE(cap, min_elements);
    EXPECT_EQ(cap & (cap - 1U), 0u) // power-of-two test
        << "capacity should be a power-of-two for the pow2 policy";

    for (int i = 0; i < 5; ++i)
        q.push(i);
    EXPECT_EQ(q.size(), 5u);

    int out;
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(q.pop(out));
        EXPECT_EQ(out, i);
    }
    EXPECT_EQ(q.size(), 0u);
}

TEST(MPSCQueueProperties, CapacityExactPolicy) {
    constexpr size_t elems_per_line =
        elements_per_cache_line<int, cache_size_policy::exact>::value;

    mpsc_queue<int, cache_size_policy::exact> q(/*min_cache_lines=*/2);
    EXPECT_EQ(q.capacity(), elems_per_line * 2);
}

/* --------------------------------------------------------------------------
 *  3. Element-type coverage
 * --------------------------------------------------------------------------*/
TEST(MPSCQueueTypes, MoveOnlyElements) {
    mpsc_queue<MoveOnly> q(1, 4);
    q.push(MoveOnly{7});
    MoveOnly out{0};
    ASSERT_TRUE(q.pop(out));
    EXPECT_EQ(out.v, 7);
}

/* --------------------------------------------------------------------------
 *  4. Index wrap-around
 * --------------------------------------------------------------------------*/
TEST(MPSCQueueCorrectness, WrapAround) {
    mpsc_queue<int> q(1, 8); // tiny buffer to force wrap
    const size_t rounds = q.capacity() * 5;

    for (size_t i = 0; i < rounds; ++i) {
        q.push(static_cast<int>(i));
        int out;
        ASSERT_TRUE(q.pop(out));
        EXPECT_EQ(out, static_cast<int>(i));
    }
}

/* --------------------------------------------------------------------------
 *  5. Back-pressure when full
 * --------------------------------------------------------------------------*/
TEST(MPSCQueueCorrectness, FullQueueBlocksUntilSpace) {
    mpsc_queue<int> q(1, 4);                  // small capacity
    const size_t max_fill = q.capacity() - 1; // queue can hold n-1 elements
    for (size_t i = 0; i < max_fill; ++i)
        q.push(static_cast<int>(i));

    std::atomic<bool> producer_done{false};
    std::thread producer([&] {
        q.push(999); // must wait
        producer_done.store(true, std::memory_order_relaxed);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(producer_done.load()); // still blocked

    int out;
    ASSERT_TRUE(q.pop(out)); // make space
    producer.join();
    EXPECT_TRUE(producer_done.load());

    for (size_t i = 1; i < max_fill; ++i) {
        ASSERT_TRUE(q.pop(out));
        EXPECT_EQ(out, static_cast<int>(i));
    }

    ASSERT_TRUE(q.pop(out));
    EXPECT_EQ(out, 999);
}

/* --------------------------------------------------------------------------
 *  6. Many-producer / single-consumer stress
 * --------------------------------------------------------------------------*/
TEST(MPSCQueueConcurrency, ManyProducersSingleConsumer) {
    constexpr size_t producers = 8;
    constexpr size_t per_thread = 20'000;
    const size_t total_items = producers * per_thread;

    mpsc_queue<uint64_t> q(/*min_cache_lines=*/32,
                           /*min_elements=*/total_items);

    std::atomic<size_t> produced{0};
    std::atomic<size_t> consumed{0};

    // consumer: pops until it has received every item
    std::thread consumer([&] {
        size_t popped = 0;
        uint64_t item;
        while (popped < total_items) {
            if (q.pop(item))
                ++popped;
        }
        EXPECT_EQ(popped, total_items);
        consumed = popped;
    });

    // producers
    std::vector<std::thread> threads;
    for (size_t id = 0; id < producers; ++id) {
        threads.emplace_back([&, id] {
            for (size_t i = 0; i < per_thread; ++i) {
                q.push((id << 32) | i); // encode producer + seq #
                ++produced;
            }
        });
    }

    for (auto& t : threads)
        t.join();
    consumer.join();
    EXPECT_EQ(produced.load(), total_items);
    EXPECT_EQ(consumed.load(), total_items);
}