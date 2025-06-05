/**
 * @file cache_utils.hpp
 * @brief Cache utility primitives for the **HQ‑LockFree** collection:
 * cache‑line padding, power‑of‑two helpers, and generic structures used by the
 *        MPSC queue and other lock‑free containers.
 *
 * This header is intentionally lightweight: no dynamic allocation, no
 * implementation code that could introduce ODR bloat – just `constexpr`
 * compile‑time helpers and POD‑style wrappers.  All templates live in the
 * `hqlockfree` namespace.
 */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hqlockfree {

/** @brief Conventional x86‑64 cache line size in bytes. */
static constexpr size_t cache_line_size = 64UL;

/**
 * @brief Helper that pads an object to occupy an entire cache line, preventing
 *        false sharing between adjacent objects.
 *
 * Inherits publicly from @p T so that it can be used anywhere a @p T is
 * expected while guaranteeing unique cache line residency.
 */
template <typename T> struct alignas(cache_line_size) cache_padded : T {
    using T::T; ///< Perfect‑forward the base constructors.
};

/* =======================================================================
 * Compile‑time utilities for powers‑of‑two arithmetic
 * =====================================================================*/

struct pow2_factory {
    /// @brief Greatest power‑of‑two **strictly less than or equal** to @p
    /// value.
    static constexpr size_t lower(const size_t value) {
        size_t current = 1;
        while (current <= value) {
            current <<= 1;
        }
        return current >> 1;
    }

    /// @brief Smallest power‑of‑two **greater than or equal** to @p value.
    static constexpr size_t upper(const size_t value) {
        size_t current = 1;
        while (current < value) {
            current <<= 1;
        }
        return current;
    }
};

struct log2_factory {
    /// @brief ⌊log2(@p value)⌋ – assumes @p value > 0.
    static constexpr size_t lower(const size_t value) {
        size_t current = 1;
        size_t out = 0;
        while (current <= value) {
            current <<= 1;
            out++;
        }
        return out - 1U;
    }

    /// @brief ⌈log2(@p value)⌉ – assumes @p value > 0.
    static constexpr size_t upper(const size_t value) {
        size_t current = 1;
        size_t out = 0;
        while (current < value) {
            current <<= 1;
            out++;
        }
        return out;
    }
};

/**
 * @brief Determines whether element packing per cache line is exact or rounded
 *        down to a power‑of‑two.
 */
enum class cache_size_policy { exact, pow2 };

/* -----------------------------------------------------------------------
 *  Compile‑time mapping: element‑count‑per‑cache‑line
 * ---------------------------------------------------------------------*/

template <typename T, cache_size_policy size_policy>
struct elements_per_cache_line {};

/// @brief *Exact* policy – pack as many @p T as physically fit.
template <typename T>
struct elements_per_cache_line<T, cache_size_policy::exact> {
    static constexpr size_t value =
        (sizeof(T) > cache_line_size) ? 1U : (cache_line_size / sizeof(T));
};

/// @brief *pow2* policy – round *down* to nearest power‑of‑two for cheap
/// bit‑masking.
template <typename T>
struct elements_per_cache_line<T, cache_size_policy::pow2> {
    static constexpr size_t value = pow2_factory::lower(
        elements_per_cache_line<T, cache_size_policy::exact>::value);
};

/* -----------------------------------------------------------------------
 *  cache_line – storage block aligned + sized to a single cache line
 * ---------------------------------------------------------------------*/

/// @brief  cache_line – storage block aligned + sized to a single cache line
template <typename T, cache_size_policy size_policy>
struct alignas(cache_line_size) cache_line
    : std::array<T, elements_per_cache_line<T, size_policy>::value> {
    using std::array<T, elements_per_cache_line<T, size_policy>::value>::array;
    static constexpr size_t number_of_elements =
        elements_per_cache_line<T, size_policy>::value;
};

/* -----------------------------------------------------------------------
 *  Index helpers – mod/div vs. bit‑mask/shift depending on policy
 * ---------------------------------------------------------------------*/

template <cache_size_policy size_policy> struct mod_indexer {};

template <> class mod_indexer<cache_size_policy::exact> {
  private:
    const size_t m_sz; ///< divisor
  public:
    mod_indexer(const size_t sz) : m_sz(sz) {}
    size_t operator()(const size_t index) const { return index % m_sz; }
};

template <> class mod_indexer<cache_size_policy::pow2> {
  private:
    const size_t m_mask; ///< bit mask (sz‑1)
  public:
    mod_indexer(const size_t sz) : m_mask(sz - 1U) {}
    size_t operator()(const size_t index) const { return index & m_mask; }
};

template <cache_size_policy size_policy> struct div_indexer {};

template <> class div_indexer<cache_size_policy::exact> {
  private:
    const size_t m_sz; ///< divisor
  public:
    div_indexer(const size_t sz) : m_sz(sz) {}
    size_t operator()(const size_t index) const { return index / m_sz; }
};

template <> class div_indexer<cache_size_policy::pow2> {
  private:
    const size_t m_shift; ///< right‑shift amount = log2(sz)
  public:
    div_indexer(const size_t sz) : m_shift(log2_factory::lower(sz)) {}
    size_t operator()(const size_t index) const { return index >> m_shift; }
};

/* -----------------------------------------------------------------------
 *  false_sharing_optimized_buffer – 2‑D view onto contiguous cache lines
 * ---------------------------------------------------------------------*/

/// @brief false_sharing_optimized_buffer – 2‑D view onto contiguous cache lines
template <typename T, cache_size_policy size_policy>
class alignas(cache_line_size) false_sharing_optimized_buffer {
  public:
    using cache_line_type = cache_line<T, size_policy>;
    /// @brief Alias for the number of elements per cache line *for this T*.
    static constexpr size_t cache_line_size =
        elements_per_cache_line<T, size_policy>::value;

  private:
    /* Helpers
     * ----------------------------------------------------------------*/
    /// @brief Ensures *pow2* policy allocates a power‑of‑two cache‑line count.
    size_t calc_min_cache_lines(size_t min_cache_lines) {
        if constexpr (size_policy == cache_size_policy::exact) {
            return min_cache_lines;
        }
        return pow2_factory::upper(min_cache_lines);
    }

    /* Data
     * --------------------------------------------------------------------*/
    std::vector<cache_line_type> m_lines;  ///< backing storage
    mod_indexer<size_policy> m_mod_index;  ///< cache‑line index
    div_indexer<size_policy> m_div_index;  ///< element index inside line
    mod_indexer<size_policy> m_mod_index2; ///< second mod for flat view

  public:
    /**
     * @brief Construct with at least @p minimum_cache_lines lines OR
     *        @p minimum_elements elements, whichever is larger.
     */
    explicit false_sharing_optimized_buffer(const size_t minimum_cache_lines,
                                            const size_t minimum_elements = 0)
        : m_lines(
              std::max(calc_min_cache_lines(minimum_cache_lines),
                       (minimum_elements +
                        (elements_per_cache_line<T, size_policy>::value - 1U)) /
                           (elements_per_cache_line<T, size_policy>::value))),
          m_mod_index(m_lines.size()), m_div_index(m_lines.size()),
          m_mod_index2(size()) {}

    /* Random access
     * -----------------------------------------------------------*/
    T& get(const size_t& idx) {
        return m_lines[m_mod_index(idx)][m_div_index(m_mod_index2(idx))];
    }
    const T& get(const size_t& idx) const {
        return m_lines[m_mod_index(idx)][m_div_index(m_mod_index2(idx))];
    }
    T& operator[](const size_t& idx) { return get(idx); }
    const T& operator[](const size_t& idx) const { return get(idx); }

    /* Introspection
     * -----------------------------------------------------------*/
    size_t number_of_cache_lines() const { return m_lines.size(); }
    size_t size() const { return number_of_cache_lines() * cache_line_size; }
};

} // namespace hqlockfree