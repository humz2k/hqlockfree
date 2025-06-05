#include <hqlockfree/daemon.hpp>
#include <hqlockfree/mpmc_fanout.hpp>
#include <hqlockfree/mpsc_queue.hpp>
#include <hqlockfree/spsc_queue.hpp>

#include <chrono>
#include <iostream>
#include <thread>

using namespace hqlockfree;

int main() {
    mpmc_fanout<int> my_fanout_queue(256);

    auto* sub = my_fanout_queue.subscribe();
    auto* sub1 = my_fanout_queue.subscribe();

    my_fanout_queue.push(10);
    my_fanout_queue.push(11);
    my_fanout_queue.push(12);
    my_fanout_queue.push(13);
    int value;
    if (sub->pop(value)) {
        std::cout << value << std::endl;
    }
    if (sub1->pop(value)) {
        std::cout << value << std::endl;
    }
    if (sub->pop(value)) {
        std::cout << value << std::endl;
    }
    if (sub1->pop(value)) {
        std::cout << value << std::endl;
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));

    /*mpsc_queue<int> my_queue(256);
    std::cout << my_queue.capacity() << std::endl;

    for (int i = 0; i < 10; i++) {
        my_queue.push(i);
        int out;
        if (my_queue.pop(out)) {
            std::cout << "got " << out << std::endl;
        }
    }

    auto callback = find_or_create_daemon()->add_callback([]() {
        // std::cout << "test" << std::endl;
        // std::this_thread::sleep_for(std::chrono::seconds(1));
    });

    std::cout << "sleeping" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "done" << std::endl;

    find_or_create_daemon()->remove_callback(callback);*/

    return 0;
}