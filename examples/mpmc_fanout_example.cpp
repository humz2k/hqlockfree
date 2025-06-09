#include <hqlockfree/mpmc_fanout.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace hqlockfree;

static std::mutex print_mutex;

struct my_subscriber {
    static inline std::atomic<size_t> g_id;
    size_t id = g_id.fetch_add(1, std::memory_order_relaxed);
    std::shared_ptr<mpmc_fanout<int>::subscription_handle> sub;
    std::atomic<bool> should_run;
    std::thread thread;

    explicit my_subscriber(mpmc_fanout<int>& queue) : sub(queue.subscribe()) {
        should_run = true;
        thread = std::thread([&]() {
            while (should_run) {
                int value;
                if (sub->pop(value)) {
                    std::lock_guard<std::mutex> lock(print_mutex);
                    std::cout << "thread" << id << ": got " << value
                              << std::endl;
                }
            }
        });
    }

    ~my_subscriber() {
        should_run = false;
        if (thread.joinable())
            thread.join();
    }
};

int main() {
    mpmc_fanout<int> my_fanout_queue(256);

    std::vector<std::unique_ptr<my_subscriber>> vecs;
    for (int i = 0; i < 3; i++) {
        vecs.emplace_back(std::make_unique<my_subscriber>(my_fanout_queue));
    }

    for (int i = 0; i < 100; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        my_fanout_queue.push(i);
    }
    return 0;
}