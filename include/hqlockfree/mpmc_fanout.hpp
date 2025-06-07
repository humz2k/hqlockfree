/**
 * @file mpmc_fanout.hpp
 * @brief Header‑only **multiple‑producer / multiple‑consumer (MPMC) fan‑out
 *        queue** built on the HQ‑LockFree primitives.
 *
 * A classic ring‑buffer supports *one* consumer.  This container lets an
 * arbitrary number of consumers subscribe to the same write stream, each with
 * its own read cursor.  Memory reclamation is cooperative: a lightweight
 * background *daemon* periodically scans all subscriptions and publishes the
 * lowest `tail` so producers know how much space remains.
 *
 * ### Key properties
 * * **Lock‑free producers** – identical hot‑path to the MPSC queue: a single
 *   `fetch_add` to reserve a slot plus a CAS to commit.
 * * **Wait‑free consumers** – each `subscription_handle` is independent and
 *   never blocks readers of other handles.
 * * **False‑sharing aware** – all shared counters are cache‑line padded; the
 *   data buffer itself is a `false_sharing_optimized_buffer`.
 */

#pragma once

#include "cache_utils.hpp"   // false_sharing_optimized_buffer & friends
#include "daemon.hpp"        // background callback engine
#include "write_confirm.hpp" // write reservation / commit helper

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace hqlockfree {

/**
 * @tparam T            Element type.
 * @tparam size_policy  Cache‑line packing policy; defaults to `pow2`.
 *
 * @class mpmc_fanout
 * @brief Lock‑free fan‑out queue with *N* producers and *M* independent
 *        consumers.
 */
template <typename T, cache_size_policy size_policy = cache_size_policy::pow2>
class mpmc_fanout {
  public:
    /**
     * @class subscription_handle
     * @brief Per‑consumer cursor into the shared ring buffer.
     *
     * Created exclusively by @ref subscribe().  Each handle tracks its own
     * `tail` pointer; calls to `pop()` are wait‑free.
     */
    class subscription_handle {
      private:
        const false_sharing_optimized_buffer<T, size_policy>&
            m_buffer;                                    ///< shared storage
        const write_confirm& m_write_confirmer;          ///< global read head
        cache_padded<std::atomic<std::uint64_t>> m_tail; ///< consumer cursor
        bool m_subscribed = true;

      public:
        explicit subscription_handle(
            const false_sharing_optimized_buffer<T, size_policy>& buffer,
            const write_confirm& write_confirmer)
            : m_buffer(buffer), m_write_confirmer(write_confirmer),
              m_tail(m_write_confirmer.get_read_index()) {}

        /** @brief Current read cursor. */
        uint64_t get_tail() const {
            return m_tail.load(std::memory_order_relaxed);
        }

        /** @brief Is this subscriber active. */
        bool subscribed() const { return m_subscribed; }

        /** @brief Unsubscribe this subscriber -> makes this subscribed invalid
         * and will deallocate memory. */
        void unsubscribe() { m_subscribed = false; }

        /**
         * @brief Pop one element if available.
         * @return `true` on success, `false` if no new data.
         */
        bool pop(T& value) {
            const uint64_t read_head = m_write_confirmer.get_read_index();
            const uint64_t tail = m_tail.load(std::memory_order_relaxed);
            if (read_head <= tail)
                return false;
            value = m_buffer[tail];
            m_tail.store(tail + 1, std::memory_order_release);
            return true;
        }
    };

  private:
    /* ------------------------------------------------------------------
     *  Shared state
     * ----------------------------------------------------------------*/
    false_sharing_optimized_buffer<T, size_policy> m_buffer;
    write_confirm m_write_confirmer;

    cache_padded<std::atomic<uint64_t>> m_min_tail = 0; ///< min(tail_i)

    const size_t m_capacity;             ///< total usable slots
    const size_t m_free_capacity_needed; ///< == capacity‑1

    /* subscriptions ----------------------------------------------------*/
    std::vector<std::unique_ptr<subscription_handle>> m_subscriptions;
    std::mutex m_subscription_mutex;

    /* daemon callback --------------------------------------------------*/
    callback_key_t m_callback_key;

    /** @brief Re‑compute global `m_min_tail` (called from background daemon).
     */
    void update_min_tail() {
        std::lock_guard<std::mutex> lock(m_subscription_mutex);
        uint64_t min_tail = m_write_confirmer.get_read_index();
        for (auto it = m_subscriptions.begin(); it != m_subscriptions.end();) {
            auto& handle = *it;
            if (handle->subscribed()) {
                min_tail = std::min(min_tail, handle->get_tail());
                ++it;
            } else {
                it = m_subscriptions.erase(it);
            }
        }
        m_min_tail.store(min_tail, std::memory_order_release);
    }

    /* Internal helpers --------------------------------------------------*/
    /// @brief Reserve one slot for the calling producer – MAY spin if full.
    uint64_t get_free_index() {
        uint64_t index = m_write_confirmer.get_write_index();
        while ((index - m_min_tail.load(std::memory_order_relaxed)) >=
               m_free_capacity_needed) {
            /* busy wait */
        }
        return index;
    }

    /// @brief Mark @p written_index as the newest committed element.
    void update_read_head(uint64_t written_index) {
        m_write_confirmer.confirm_write(written_index);
    }

  public:
    /**
     * @brief Construct a buffer with at least @p min_cache_lines lines *or*
     *        @p min_elements elements.
     */
    explicit mpmc_fanout(size_t min_cache_lines, size_t min_elements = 0)
        : m_buffer(min_cache_lines, min_elements), m_capacity(m_buffer.size()),
          m_free_capacity_needed(m_capacity - 1UL) {
        m_callback_key = find_or_create_daemon()->add_callback(
            [&]() { this->update_min_tail(); });
    }

    /** Cancel daemon callback. */
    ~mpmc_fanout() { find_or_create_daemon()->remove_callback(m_callback_key); }

    /* Non‑movable / non‑copyable ---------------------------------------*/
    mpmc_fanout(const mpmc_fanout&) = delete;
    mpmc_fanout& operator=(const mpmc_fanout&) = delete;
    mpmc_fanout(mpmc_fanout&&) = delete;
    mpmc_fanout& operator=(mpmc_fanout&&) = delete;

    /* ------------------------------------------------------------------
     *  Consumer API
     * ----------------------------------------------------------------*/
    /**
     * @brief Create and return a new independent subscription.
     *
     * The caller owns the returned pointer; destroying the `unique_ptr`
     * removes it from the fan‑out set automatically.
     */
    [[nodiscard]] subscription_handle* subscribe() {
        std::lock_guard<std::mutex> lock(m_subscription_mutex);
        return m_subscriptions
            .emplace_back(std::make_unique<subscription_handle>(
                m_buffer, m_write_confirmer))
            .get();
    }

    /* ------------------------------------------------------------------
     *  Introspection
     * ----------------------------------------------------------------*/
    /// @return The current number of elements available to the consumer.
    size_t size() const {
        return m_write_confirmer.get_read_index() -
               m_min_tail.load(std::memory_order_relaxed);
    }
    /// @return The ring size.
    size_t capacity() const { return m_capacity; }

    /* ------------------------------------------------------------------
     *  Producer API
     * ----------------------------------------------------------------*/
    void push(const T& value) {
        uint64_t index = get_free_index();
        m_buffer[index] = value;
        update_read_head(index);
    }
    void push(T&& value) {
        uint64_t index = get_free_index();
        m_buffer[index] = std::move(value);
        update_read_head(index);
    }
};

} // namespace hqlockfree