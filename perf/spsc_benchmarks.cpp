#include <benchmark/benchmark.h>

#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <hqlockfree/mpmc_fanout.hpp>
#include <hqlockfree/mpsc_queue.hpp>
#include <hqlockfree/spsc_queue.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

static constexpr size_t queue_size = 1024 << 4;

enum class queue_type { spsc, mpsc, fanout, boost_spsc, boost_mpsc, mutex };

template <typename T, queue_type type> struct queue_wrapper {};

template <typename T>
struct queue_wrapper<T, queue_type::spsc> : public hqlockfree::spsc_queue<T> {
    explicit queue_wrapper(size_t n_elements)
        : hqlockfree::spsc_queue<T>(0, n_elements) {}
};

template <typename T>
struct queue_wrapper<T, queue_type::mpsc> : public hqlockfree::mpsc_queue<T> {
    explicit queue_wrapper(size_t n_elements)
        : hqlockfree::mpsc_queue<T>(0, n_elements) {}
};

template <typename T>
struct queue_wrapper<T, queue_type::fanout>
    : public hqlockfree::mpmc_fanout<T> {
    std::shared_ptr<typename hqlockfree::mpmc_fanout<T>::subscription_handle>
        sub;
    explicit queue_wrapper(size_t n_elements)
        : hqlockfree::mpmc_fanout<T>(0, n_elements), sub(this->subscribe()) {}

    bool pop(T& value) { return sub->pop(value); }
};

template <typename T> struct queue_wrapper<T, queue_type::boost_mpsc> {
    boost::lockfree::queue<T> queue;
    explicit queue_wrapper(size_t n_elements) : queue(n_elements) {}

    void push(const T& value) {
        while (!queue.push(value)) {
        }
    }

    bool pop(T& value) { return queue.pop(value); }
};

template <typename T> struct queue_wrapper<T, queue_type::boost_spsc> {
    boost::lockfree::spsc_queue<T> queue;
    explicit queue_wrapper(size_t n_elements) : queue(n_elements) {}

    void push(const T& value) {
        while (!queue.push(value)) {
        }
    }

    bool pop(T& value) { return queue.pop(value); }
};

template <typename T> struct queue_wrapper<T, queue_type::mutex> {
    explicit queue_wrapper([[maybe_unused]] size_t n_elements) {}
    std::queue<T> queue;
    std::mutex mutex;
    void push(const T& value) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(value);
    }
    void push(T&& value) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(std::move(value));
    }
    bool pop(T& value) {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.size() == 0)
            return false;
        value = queue.front();
        queue.pop();
        return true;
    }
};

template <queue_type type>
static void callsite_push_latency_single_producer(benchmark::State& st) {
    queue_wrapper<size_t, type> q(queue_size);
    std::atomic<bool> should_run = true;
    std::atomic_flag started = false;

    std::thread thread([&]() {
        started.test_and_set();
        started.notify_all();
        size_t next = 0;

        while (should_run.load(std::memory_order_relaxed)) {
            size_t out = 0;
            bool popped = q.pop(out);
            if (popped) {
                if (out != (next++)) {
                    throw std::runtime_error("oops");
                }
            }
            benchmark::DoNotOptimize(popped);
            benchmark::DoNotOptimize(out);
        }
    });

    started.wait(false);

    size_t iteration = 0;
    for (auto _ : st) {
        q.push(iteration++);
    }

    should_run = false;
    if (thread.joinable())
        thread.join();

    st.SetItemsProcessed(st.iterations());
}

template <queue_type type>
static void roundtrip_single_producer(benchmark::State& st) {
    queue_wrapper<size_t, type> q1(queue_size);
    queue_wrapper<size_t, type> q2(queue_size);
    std::atomic<bool> should_run = true;
    std::atomic_flag started = false;

    std::thread thread([&]() {
        started.test_and_set();
        started.notify_all();

        while (should_run.load(std::memory_order_relaxed)) {
            size_t out = 0;
            if (q1.pop(out)) {
                q2.push(out);
            }
        }
    });

    started.wait(false);

    size_t iteration = 0;
    for (auto _ : st) {
        const size_t to_send = iteration++;
        q1.push(to_send);
        size_t to_recv = 0;
        while (!q2.pop(to_recv)) {
        }
        if (to_send != to_recv) {
            throw std::runtime_error("oops");
        }
    }

    should_run = false;
    if (thread.joinable())
        thread.join();

    st.SetItemsProcessed(st.iterations());
}

template <queue_type type>
static void roundtrip_single_thread(benchmark::State& st) {
    queue_wrapper<size_t, type> q1(queue_size);

    size_t iteration = 0;
    for (auto _ : st) {
        const size_t to_send = iteration++;
        q1.push(to_send);
        size_t to_recv = 0;
        q1.pop(to_recv);
        if (to_recv != to_send) {
            throw std::runtime_error("oops");
        }
    }

    st.SetItemsProcessed(st.iterations());
}

BENCHMARK(callsite_push_latency_single_producer<queue_type::spsc>)->Args({});
BENCHMARK(callsite_push_latency_single_producer<queue_type::mpsc>)->Args({});
BENCHMARK(callsite_push_latency_single_producer<queue_type::fanout>)->Args({});
BENCHMARK(callsite_push_latency_single_producer<queue_type::boost_spsc>)
    ->Args({});
BENCHMARK(callsite_push_latency_single_producer<queue_type::boost_mpsc>)
    ->Args({});
BENCHMARK(callsite_push_latency_single_producer<queue_type::mutex>)->Args({});

BENCHMARK(roundtrip_single_producer<queue_type::spsc>)->Args({});
BENCHMARK(roundtrip_single_producer<queue_type::mpsc>)->Args({});
BENCHMARK(roundtrip_single_producer<queue_type::fanout>)->Args({});
BENCHMARK(roundtrip_single_producer<queue_type::boost_spsc>)->Args({});
BENCHMARK(roundtrip_single_producer<queue_type::boost_mpsc>)->Args({});
BENCHMARK(roundtrip_single_producer<queue_type::mutex>)->Args({});

BENCHMARK(roundtrip_single_thread<queue_type::spsc>)->Args({});
BENCHMARK(roundtrip_single_thread<queue_type::mpsc>)->Args({});
BENCHMARK(roundtrip_single_thread<queue_type::fanout>)->Args({});
BENCHMARK(roundtrip_single_thread<queue_type::boost_spsc>)->Args({});
BENCHMARK(roundtrip_single_thread<queue_type::boost_mpsc>)->Args({});
BENCHMARK(roundtrip_single_thread<queue_type::mutex>)->Args({});

BENCHMARK_MAIN();