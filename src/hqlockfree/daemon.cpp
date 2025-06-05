#include <hqlockfree/daemon.hpp>

#include <memory>

namespace hqlockfree {

void daemon::run_callbacks() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [key, callback] : m_callbacks) {
        callback();
    }
}

void daemon::run() {
    while (m_should_run.load(std::memory_order_seq_cst)) {
        run_callbacks();
    }
}

daemon::daemon() {
    m_should_run = true;
    m_thread = std::thread([&]() { this->run(); });
}

daemon::~daemon() {
    m_should_run.store(false, std::memory_order_seq_cst);
    if (m_thread.joinable())
        m_thread.join();
}

callback_key_t daemon::add_callback(std::function<void()> func) {
    std::lock_guard<std::mutex> lock(m_mutex);
    callback_key_t key = m_next_callback++;
    m_callbacks[key] = func;
    return key;
}

void daemon::remove_callback(callback_key_t key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_callbacks.find(key) == m_callbacks.end())
        return;
    m_callbacks.erase(key);
}

daemon* find_or_create_daemon() {
    static std::unique_ptr<daemon> g_daemon;
    static std::mutex g_mutex;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_daemon) {
        g_daemon = std::make_unique<daemon>();
    }
    return g_daemon.get();
}

} // namespace hqlockfree