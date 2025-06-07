#include <hqlockfree/spmc_push_vec.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <unordered_set>

using namespace hqlockfree;

TEST(SPMCPushVecBasic, PushSingleThread) {
    spmc_push_vec<int> vec(4);

    size_t expected_size = 0UL;
    for (int i = 0; i < 100; i++) {
        EXPECT_EQ(vec.size(), expected_size);
        vec.push_back(i);
        expected_size++;
        EXPECT_EQ(vec.size(), expected_size);
        EXPECT_EQ(vec[expected_size - 1ul], i);
    }
}

TEST(SPMCPushVecBasic, PushSingleThreadAndDrop) {
    spmc_push_vec<int> vec(4);

    size_t expected_size = 0UL;
    for (int i = 0; i < 100; i++) {
        EXPECT_EQ(vec.size(), expected_size);
        vec.push_back(i);

        if ((i % 20) == 0) {
            vec.drop_old();
        }

        expected_size++;
        EXPECT_EQ(vec.size(), expected_size);
        EXPECT_EQ(vec[expected_size - 1ul], i);
    }
}

TEST(SPMCPushVecBasic, CapacityGrowth) {
    spmc_push_vec<int> vec(1 /* tiny */);
    std::size_t old_cap = vec.capacity();
    vec.push_back(1);
    vec.push_back(2); // should force re-alloc
    if (old_cap < 2ul) {
        EXPECT_GT(vec.capacity(), old_cap);
    }
    // verify iterator validity obtained *before* re-alloc.
    auto first_it = vec.begin();
    EXPECT_EQ(*first_it, 1);
}

TEST(SPMCPushVecCorrectness, ResizeRules) {
    spmc_push_vec<int> vec(2);
    vec.resize(5);
    EXPECT_EQ(vec.size(), 5u);
    EXPECT_THROW(vec.resize(3), std::runtime_error);
}

TEST(SPMCPushVecIterators, ForwardIteratorContract) {
    spmc_push_vec<int> vec;
    for (int i = 0; i < 5; ++i)
        vec.push_back(i);

    // mutable iteration
    int sum = 0;
    for (auto& v : vec)
        sum += v;
    EXPECT_EQ(sum, 0 + 1 + 2 + 3 + 4);

    // const iteration
    const auto& cvec = vec;
    int product = 1;
    for (auto it = cvec.begin(); it != cvec.end(); ++it)
        product *= *it + 1;
    EXPECT_EQ(product, 2 * 3 * 4 * 5);
}

TEST(SPMCPushVecConcurrency, ProducerAndManyReaders) {

    constexpr std::size_t readers = 8;
    constexpr std::size_t pushes = 1000;

    spmc_push_vec<std::uint64_t> vec(128);

    std::atomic<bool> stop{false};
    std::vector<std::thread> reader_threads;
    std::vector<std::unordered_set<std::uint64_t>> seen(readers);

    for (std::size_t r = 0; r < readers; ++r) {
        reader_threads.emplace_back([&, r] {
            const auto& cvec = vec;
            while (!stop.load(std::memory_order_relaxed)) {
                size_t count = 0;
                for (auto& i : cvec) {
                    count += i;
                }
                seen[r].insert(count);
            }
        });
    }

    for (std::uint64_t i = 0; i < pushes; ++i)
        vec.push_back(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    stop = true;

    for (auto& t : reader_threads)
        t.join();
    for (const auto& s : seen) {
        EXPECT_NE(s.find(pushes), s.end());
    }
}

TEST(SPMCPushVecConcurrency, IteratorSurvivesRealloc) {
    spmc_push_vec<int> vec(2);
    vec.push_back(1);
    auto it_before = vec.begin(); // points into first allocation
    vec.push_back(2);             // may reallocate
    vec.push_back(3);

    EXPECT_EQ(*it_before, 1); // iterator still valid
}