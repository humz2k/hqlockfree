#include <hqlockfree/spmc_push_vec.hpp>

#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

using namespace hqlockfree;

int main() {
    spmc_push_vec<int> my_vec;

    std::mutex print_mutex;

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; i++) {
        threads.emplace_back([&, i]() {
            const auto& const_vec_view = my_vec;
            for (int j = 0; j < 10; j++) {
                {
                    auto size = const_vec_view.size();
                    std::lock_guard<std::mutex> lock(print_mutex);
                    std::cout << "thread" << i << " sees size=" << size
                              << std::endl;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            auto size = const_vec_view.size();
            std::lock_guard<std::mutex> lock(print_mutex);
            std::cout << "thread" << i << " sees size=" << size << std::endl;
        });
    }

    for (int i = 0; i < 1000; i++) {
        my_vec.push_back(i);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    for (auto& thread : threads) {
        if (thread.joinable())
            thread.join();
    }
    return 0;
}