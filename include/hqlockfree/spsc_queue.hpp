/**
 * @file spsc_queue.hpp
 * @brief Header‑only **single‑producer / single‑consumer (SPSC) ring buffer**
 *        for high‑throughput pipelines.
 *
 * The implementation leverages the same HQ‑LockFree primitives as the MPSC and
 * MPMC variants but relaxes the concurrency requirements to a *single* writer
 * and *single* reader.  This permits an even simpler algorithm:
 *
 * * producer increments a private `m_private_head` (no atomics) to reserve
 *   space, then publishes the write via `m_head.store()`;
 * * consumer polls `m_head` to discover the upper bound of readable slots and
 *   advances its private `m_tail` when data is consumed.
 *
 * **False sharing is actively mitigated** via `cache_padded` for the shared
 * indices and by placing the producer‑only `m_private_head` on its own aligned
 * cache line.
 *
 * ## Complexity
 * * `push()` – *O(1)* (busy‑wait if the ring is full).
 * * `pop()`  – *O(1)* / wait‑free.
 *
 * ## Memory ordering
 * * Producer        – `fetch_add(relaxed)` on the private head, then a single
 *   `store(release)` when data is fully written.
 * * Consumer        – `load(acquire)` on the public head, ensuring visibility
 *   of the preceding write.
 */

#pragma once

#include "cache_utils.hpp" // false_sharing_optimized_buffer & padding helpers

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hqlockfree {

/**
 * @tparam T            Element type.
 * @tparam size_policy  Cache‑line packing policy (default: power‑of‑two).
 *
 * @class spsc_queue
 * @brief Minimal lock‑free ring buffer for one producer and one consumer.
 */
template <typename T, cache_size_policy size_policy = cache_size_policy::pow2>
class spsc_queue {
  private:
    /* ------------------------------------------------------------------
     *  Storage
     * ----------------------------------------------------------------*/
    false_sharing_optimized_buffer<T, size_policy> m_buffer;

    alignas(cache_line_size) std::uint64_t m_private_head{0}; ///< producer‑only
    cache_padded<std::atomic<std::uint64_t>> m_head{
        0}; ///< public head (producer → consumer)
    cache_padded<std::atomic<std::uint64_t>> m_tail{0}; ///< consumer cursor

    const size_t m_capacity;             ///< total usable slots
    const size_t m_free_capacity_needed; ///< == capacity‑1

    uint64_t get_free_index() {
        uint64_t index = m_private_head++;
        while ((index - m_tail.load(std::memory_order_relaxed)) >=
               m_free_capacity_needed) {
            /* busy wait */
        }
        return index;
    }

    /// @brief Mark @p written_index as the newest committed element.
    void update_read_head(uint64_t written_index) {
        m_head.store(written_index + 1, std::memory_order_release);
    }

  public:
    /**
     * @brief Construct with at least @p min_cache_lines lines or
     *        @p min_elements elements.
     */
    explicit spsc_queue(std::size_t min_cache_lines,
                        std::size_t min_elements = 0)
        : m_buffer(min_cache_lines, min_elements), m_capacity(m_buffer.size()),
          m_free_capacity_needed(m_capacity - 1UL) {}

    spsc_queue(const spsc_queue&) = delete;
    spsc_queue& operator=(const spsc_queue&) = delete;
    spsc_queue(spsc_queue&&) = delete;
    spsc_queue& operator=(spsc_queue&&) = delete;

    size_t capacity() const { return m_capacity; }

    size_t size() const {
        auto current_tail = m_tail.load(std::memory_order_acquire);
        return m_head.load(std::memory_order_acquire) - current_tail;
    }

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

    /* ------------------------------------------------------------------
     *  Consumer API
     * ----------------------------------------------------------------*/
    bool pop(T& value) {
        uint64_t index = m_tail.load(std::memory_order_relaxed);
        uint64_t head = m_head.load(std::memory_order_acquire);
        if (index >= head)
            return false;
        value = std::move(m_buffer[index]);
        m_tail.store(index + 1, std::memory_order_release);
        return true;
    }
};

} // namespace hqlockfree