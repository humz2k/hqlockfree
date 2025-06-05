/**
 * @file write_confirm.hpp
 * @brief Lightweight helper that pairs a monotonically‑increasing *write head*
 *        with a *read head* to allow producers to reserve slots and later
 *        *confirm* their writes.
 *
 * This utility is intended for internal use by HQ‑LockFree containers (most
 * notably the MPSC queue) but is perfectly usable on its own whenever you need
 * a minimal *“reserve‑then‑commit”* synchronisation primitive.
 *
 * ## Semantics
 * 1. **get_write_index()** – Atomically fetch‑and‑increment the global write
 *    counter.  The returned value is the caller's unique slot.
 * 2. **confirm_write(index)** – Once the producer has fully written the data
 *    into its slot it *commits* the change, advancing the read head from
 *    *index* to *index + 1*.  The CAS loop ensures monotonically‑ordered
 *    commits even when multiple producers finish out‑of‑order.
 * 3. **get_read_index()** – The consumer polls this to discover how far
 *    producers have progressed.
 *
 * Both counters are individually padded to separate cache lines, eliminating
 * false sharing between concurrent producers and the consumer thread.
 */

#pragma once

#include "cache_utils.hpp" // cache_padded & cache_line_size

#include <atomic>
#include <cstdint>

namespace hqlockfree {

/**
 * @class write_confirm
 * @brief Multi‑producer commit barrier.
 */
class write_confirm {
  private:
    cache_padded<std::atomic<std::uint64_t>> m_write_head{
        0}; ///< next free index
    cache_padded<std::atomic<std::uint64_t>> m_read_head{
        0}; ///< next committed index

  public:
    /**
     * @brief Reserve a slot for writing.
     * @return The caller‑exclusive index to write to.
     *
     * Thread‑safe for *multiple* concurrent producers.
     */
    uint64_t get_write_index() {
        return m_write_head.fetch_add(1, std::memory_order_acq_rel);
    }

    /**
     * @brief Snapshot the consumer‑visible *read head*.
     * @return The read head index
     */
    uint64_t get_read_index() const {
        return m_read_head.load(std::memory_order_acquire);
    }

    /**
     * @brief Commit the element at @p written_index and make it visible to the
     *        consumer.
     *
     * The CAS loop bumps the read head from `written_index` to
     * `written_index + 1`.  If another producer has already advanced past that
     * point we simply exit early – the work is already done.
     */
    void confirm_write(uint64_t written_index) {
        uint64_t expected = written_index;
        const uint64_t desired = written_index + 1;
        while (!m_read_head.compare_exchange_weak(expected, desired,
                                                  std::memory_order_release)) {
            if (expected >= desired)
                break;                // this shouldn't happen...
            expected = written_index; // reset expected on failure
        }
    }
};

} // namespace hqlockfree