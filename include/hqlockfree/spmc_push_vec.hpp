/**
 * @file spmc_push_vec.hpp
 * @brief Header‑only **single‑producer / multi‑consumer (SPMC) append‑only
 *        vector** that offers *lock‑free* push‑back for the producer and
 *        constant‑time read‑only access for an arbitrary number of consumer
 *        threads.
 *
 * ## Concurrency model
 * * **ONE** dedicated producer thread may *append* elements with `push_back()`
 *   or `emplace_back()`.  *Erase* or *shrink* operations are **not** provided
 *   by design—once appended, an element lives for the lifetime of the
 *   container.
 * * **MANY** consumer threads obtain a **const reference** to the container
 *   (or iterate via `begin()/end()`).  They must treat the data as
 * **read‑only**; if an element is itself mutable, it is up to the programmer to
 * guarantee those writes are atomic with respect to other readers.
 *
 * ## Implementation highlights
 * 1. The producer owns a pointer (`m_current_vec`) to the *active* std::vector.
 *    When a capacity expansion is needed, a **new** vector is copied (move
 *    constructed) from the old one, reserved with doubled capacity, and the
 *    atomic pointer is swapped with `release` semantics.  Consumers always load
 *    the pointer with `relaxed` ordering, which is safe because the producer’s
 *    `release` publish pairs with the consumer’s data‑dependency on the loaded
 *    pointer.
 * 2. The current *size* is published via `m_size` (`release` on write,
 *    `relaxed` on read).  Consumers can call `size()` at any time to bound
 *    iteration.
 * 3. Every historical vector is stored in `m_old_vecs` so that iterators
 *    obtained before a reallocation remain valid.  This trades some memory for
 *    **zero copy on reads** and iterator stability.
 *
 * ### Complexity
 * | Operation       | Complexity | Notes                        |
 * |-----------------|-----------:|------------------------------|
 * | `push_back`     | *Amortised* **O(1)** | May copy twice per doubling |
 * | `emplace_back`  | *Amortised* **O(1)** | —                         |
 * | `operator[]`    | **O(1)**   | No bounds checks             |
 * | Iteration       | **O(N)**   | Forward iterator             |
 *
 * @warning This container **does not** support erase or shrink.  Attempting to
 *          call `resize()` with a smaller size will throw.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <iterator>
#include <memory>
#include <vector>

namespace hqlockfree {

/**
 * @tparam T Element type.
 * @tparam allocator allocator.
 *
 * @class spmc_push_vec
 * @brief Lock‑free append‑only vector with a single producer and multiple
 *        concurrent readers.
 */
template <typename T, typename allocator = std::allocator<T>>
class spmc_push_vec {
  private:
    /** @brief Historical vectors kept alive for iterator stability */
    std::vector<std::unique_ptr<std::vector<T, allocator>>> m_old_vecs;
    /** @brief Pointer to the vector currently used for push_back */
    std::atomic<std::vector<T, allocator>*> m_current_vec = nullptr;
    /** @brief Logical size – published by producer, read by consumers */
    std::atomic<size_t> m_size = 0;

    /** @brief Allocate a new backing vector and track it in m_old_vecs */
    std::vector<T, allocator>*
    create_new_vector(const std::vector<T>& previous = {}) {
        return m_old_vecs
            .emplace_back(std::make_unique<std::vector<T>>(previous))
            .get();
    }

    /* Helpers to load the active vector  */
    std::vector<T, allocator>& get_current_vec() {
        return *(m_current_vec.load(std::memory_order_acquire));
    }
    const std::vector<T, allocator>& get_current_vec() const {
        return *(m_current_vec.load(std::memory_order_acquire));
    }

  public:
    /* ====================================================================
     *  Iterators (mutable & const) – stable even across re‑allocations
     * =================================================================*/
    struct iterator {
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using pointer = value_type*;
        using reference = value_type&;

      private:
        spmc_push_vec& m_vec;
        size_t m_idx;

      public:
        explicit iterator(spmc_push_vec& vec, size_t idx)
            : m_vec(vec), m_idx(idx) {}
        bool operator==(const iterator& other) const {
            return (m_idx == other.m_idx) && (&other.m_vec == &m_vec);
        }
        bool operator!=(const iterator& other) const {
            return (m_idx != other.m_idx) || (&other.m_vec != &m_vec);
        }
        reference operator*() { return m_vec[m_idx]; }
        pointer operator->() { return &m_vec[m_idx]; }
        iterator& operator++() {
            m_idx++;
            return *this;
        }
        iterator operator++(int) {
            iterator tmp = *this;
            ++(*this);
            return tmp;
        }
    };

    struct const_iterator {
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using pointer = const value_type*;
        using reference = const value_type&;

      private:
        const spmc_push_vec& m_vec;
        size_t m_idx;

      public:
        explicit const_iterator(const spmc_push_vec& vec, size_t idx)
            : m_vec(vec), m_idx(idx) {}
        bool operator==(const const_iterator& other) const {
            return (m_idx == other.m_idx) && (&other.m_vec == &m_vec);
        }
        bool operator!=(const const_iterator& other) const {
            return (m_idx != other.m_idx) || (&other.m_vec != &m_vec);
        }
        reference operator*() const { return m_vec[m_idx]; }
        pointer operator->() const { return &m_vec[m_idx]; }
        const_iterator& operator++() {
            m_idx++;
            return *this;
        }
        const_iterator operator++(int) {
            const_iterator tmp = *this;
            ++(*this);
            return tmp;
        }
    };

    /* ==================================================================
     *  Constructor & capacity helpers
     * =================================================================*/
    explicit spmc_push_vec(size_t initial_capacity = 256)
        : m_current_vec(create_new_vector()) {
        get_current_vec().reserve(initial_capacity);
    }

    [[nodiscard]] size_t capacity() const {
        return get_current_vec().capacity();
    }
    [[nodiscard]] size_t size() const {
        return m_size.load(std::memory_order_acquire);
    }

    /** @brief Ensure capacity is at least @p elements. */
    void reserve(size_t elements) {
        if (capacity() < elements) {
            auto* new_vec = create_new_vector(get_current_vec());
            new_vec->reserve(elements);
            m_current_vec.store(new_vec, std::memory_order_release);
        }
    }

    /** @brief Drop all old vectors (dangerous...). */
    void drop_old() {
        auto current = std::move(*m_old_vecs.rbegin());
        m_old_vecs.clear();
        m_old_vecs.emplace_back(std::move(current));
    }

    /** @brief Resize only *upwards*.  Shrink requests throw. */
    void resize(size_t elements) {
        if (elements < size()) {
            throw std::runtime_error("spmc_push_vec::resize - cannot shrink");
        }
        reserve(elements);
        get_current_vec().resize(elements);
        m_size.store(elements, std::memory_order_release);
    }

    /* ==================================================================
     *  Producer‑only push / emplace
     * =================================================================*/
    void push_back(const T& element) {
        const auto current_size = size();
        if (current_size >= capacity()) {
            reserve(capacity() * 2UL);
        }
        get_current_vec().push_back(element);
        m_size.store(current_size + 1, std::memory_order_release);
    }

    void push_back(T&& element) {
        const auto current_size = size();
        if (current_size >= capacity()) {
            reserve(capacity() * 2UL);
        }
        get_current_vec().push_back(std::move(element));
        m_size.store(current_size + 1, std::memory_order_release);
    }

    template <typename... Args> T& emplace_back(Args&&... args) {
        const auto current_size = size();
        if (current_size >= capacity()) {
            reserve(capacity() * 2UL);
        }
        auto& out = get_current_vec().emplace_back(std::forward<Args>(args)...);
        m_size.store(current_size + 1, std::memory_order_release);
        return out;
    }

    /* ==================================================================
     *  Element access (read‑only for consumers)
     * =================================================================*/
    T& operator[](size_t idx) { return get_current_vec()[idx]; }
    const T& operator[](size_t idx) const { return get_current_vec()[idx]; }

    /* iterators */
    [[nodiscard]] iterator begin() { return iterator(*this, 0); }
    [[nodiscard]] iterator end() { return iterator(*this, size()); }
    [[nodiscard]] const_iterator begin() const {
        return const_iterator(*this, 0);
    }
    [[nodiscard]] const_iterator end() const {
        return const_iterator(*this, size());
    }
};

} // namespace hqlockfree