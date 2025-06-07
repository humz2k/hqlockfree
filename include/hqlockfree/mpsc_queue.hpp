/**
 * @file mpsc_queue.hpp
 * @brief Header‑only lock‑free *multiple‑producer / single‑consumer* (MPSC)
 *        ring buffer that minimises false sharing.
 *
 * ## Motivation & design goals
 * 1. **Low latency / high throughput** – All critical paths are single
 *    compare‑and‑swap or fetch‑and‑add operations with relaxed ordering where
 *    safe.
 * 2. **Cache‑friendliness** – Data and control variables are carefully padded
 *    so that distinct producers never fight over the same cache line.
 * 3. **Configurable element packing** – The number of elements stored per
 *    cache line can be either the *exact* mathematical result or the greatest
 *    power‑of‑two not exceeding it, giving the user freedom to trade off space
 *    vs. branchless bit‑masking.
 */

#pragma once

#include "cache_utils.hpp"
#include "write_confirm.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hqlockfree {

/* -----------------------------------------------------------------------
 *  queue – lock‑free MPSC ring buffer
 * ---------------------------------------------------------------------*/

template <typename T, cache_size_policy size_policy = cache_size_policy::pow2>
class mpsc_queue {
  private:
    /* State -------------------------------------------------------------*/
    false_sharing_optimized_buffer<T, size_policy> m_buffer;

    write_confirm m_write_confirm;

    cache_padded<std::atomic<uint64_t>> m_tail = 0; ///< first un‑consumed slot

    const size_t m_capacity;             ///< total usable slots
    const size_t m_free_capacity_needed; ///< == capacity‑1

    /* Internal helpers --------------------------------------------------*/
    /// @brief Reserve one slot for the calling producer – MAY spin if full.
    uint64_t get_free_index() {
        uint64_t index = m_write_confirm.get_write_index();
        while ((index - m_tail.load(std::memory_order_relaxed)) >=
               m_free_capacity_needed) {
            /* busy wait */
        }
        return index;
    }

    /// @brief Mark @p written_index as the newest committed element.
    void update_read_head(uint64_t written_index) {
        m_write_confirm.confirm_write(written_index);
    }

  public:
    /**
     * @brief Create a queue with at least @p min_cache_lines cache lines, or
     *        enough capacity to hold @p min_elements, whichever results in the
     *        larger buffer.
     *
     * @note For an MPSC ring the usable capacity is *capacity‑1*; one slot is
     *       intentionally left vacant to distinguish full vs. empty states.
     */
    explicit mpsc_queue(size_t min_cache_lines, size_t min_elements = 0)
        : m_buffer(min_cache_lines, min_elements), m_capacity(m_buffer.size()),
          m_free_capacity_needed(m_capacity - 1UL) {}

    /* Non‑movable / non‑copyable ---------------------------------------*/
    mpsc_queue(const mpsc_queue&) = delete;
    mpsc_queue& operator=(const mpsc_queue&) = delete;
    mpsc_queue(mpsc_queue&&) = delete;
    mpsc_queue& operator=(mpsc_queue&&) = delete;

    /* Introspection -----------------------------------------------------*/
    /// @return The current number of elements available to the consumer.
    size_t size() const {
        return m_write_confirm.get_read_index() -
               m_tail.load(std::memory_order_relaxed);
    }
    /// @return The ring size.
    size_t capacity() const { return m_capacity; }

    /* Producer API ------------------------------------------------------*/
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

    /* Consumer API ------------------------------------------------------*/
    bool pop(T& value) {
        const uint64_t read_head = m_write_confirm.get_read_index();
        const uint64_t tail = m_tail.load(std::memory_order_relaxed);
        if (read_head <= tail)
            return false;
        value = std::move(m_buffer[tail]);
        m_tail.store(tail + 1, std::memory_order_release);
        return true;
    }
};

} // namespace hqlockfree