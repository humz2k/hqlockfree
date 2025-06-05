/**
 * @file daemon.hpp
 * @brief Tiny single‑threaded *callback dispatcher* that lives for the process
 *        lifetime.
 *
 * A `hqlockfree::daemon` spawns one background thread that periodically walks a
 * registry of user‑supplied *void() -> void* callbacks.  The class offers a
 * simple, contention‑free API:
 *
 * ```cpp
 * auto id = d.add_callback([]{ ... });
 * ...
 * d.remove_callback(id);
 * ```
 *
 * The dispatcher is thread‑safe:
 *  * **add_callback** / **remove_callback** may be invoked concurrently from
 *    any thread.
 *  * The callback map is guarded by an internal `std::mutex`.
 *
 * The helper `find_or_create_daemon()` gives the rest of the library a
 * convenient process‑wide singleton.  If you need multiple independent
 * daemons, simply create them directly.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace hqlockfree {

/// @brief Handle returned by @ref daemon::add_callback and understood by @ref
/// daemon::remove_callback.
using callback_key_t = uint64_t;

/**
 * @class daemon
 * @brief Background thread that repeatedly executes registered callbacks.
 */
class daemon {
  private:
    std::atomic<bool> m_should_run{true}; ///< Exit flag
    std::thread m_thread;                 ///< Worker thread
    std::mutex m_mutex;                   ///< Protects map below

    /// @brief Registered callbacks, keyed by monotonically increasing id.
    std::unordered_map<callback_key_t, std::function<void()>> m_callbacks;
    callback_key_t m_next_callback = 0; ///< id generator

    /// @brief Execute every callback *once*; called from the worker thread.
    void run_callbacks();

    /// @brief Main event‑loop for the worker thread.
    void run();

  public:
    /**
     * @brief Construct and immediately launch the worker thread.
     */
    daemon();

    /**
     * @brief Signal the worker thread to stop and join() during destruction.
     */
    ~daemon();

    /**
     * @brief Register @p func to be executed in the background thread.
     *
     * @return A unique opaque token that can later be passed to
     *         @ref remove_callback.
     */
    [[nodiscard]] callback_key_t add_callback(std::function<void()> func);

    /**
     * @brief Remove a previously registered callback.
     *
     * Safe to call even if the callback is currently executing – the next
     * iteration simply won’t see it.
     */
    void remove_callback(callback_key_t key);
};

/**
 * @brief Obtain a process‑wide singleton daemon.  Thread‑safe and idempotent.
 */
[[nodiscard]] daemon* find_or_create_daemon();

} // namespace hqlockfree