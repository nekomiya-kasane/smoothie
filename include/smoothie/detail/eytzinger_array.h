#pragma once

/// @file detail/eytzinger_array.h
/// @brief Cache-friendly Eytzinger (BFS) layout for branchless binary search.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace smoothie::detail {

template <typename T> class eytzinger_array {
  public:
    eytzinger_array() = default;

    /// Build the Eytzinger layout from a sorted input array.
    /// Uses iterative in-order traversal with explicit stack (O(n) time, O(log n) stack).
    void build(std::span<const T> sorted) {
        const auto n = sorted.size();
        data_.resize(n);
        sorted_index_.resize(n);
        if (n == 0) {
            return;
        }

        // Iterative in-order traversal using explicit stack.
        // Each frame: (eytz_idx, lo, hi, phase).
        // phase 0 = descend left, phase 1 = visit + descend right.
        struct frame {
            size_t eytz_idx, lo, hi;
            uint8_t phase;
        };
        // Max depth = ceil(log2(n+1)) + 1, always < 64 for any practical n.
        frame stack[64];
        int sp = 0;
        stack[sp] = {0, 0, n, 0};
        size_t sorted_pos = 0;

        while (sp >= 0) {
            auto &f = stack[sp];
            if (f.eytz_idx >= n || f.lo >= f.hi) {
                --sp;
                continue;
            }
            if (f.phase == 0) {
                // Descend left
                f.phase = 1;
                size_t left = 2 * f.eytz_idx + 1;
                if (left < n && f.lo < f.hi) {
                    ++sp;
                    stack[sp] = {left, f.lo, f.hi, 0};
                }
            } else {
                // Visit node: assign next sorted element
                data_[f.eytz_idx] = sorted[sorted_pos];
                sorted_index_[f.eytz_idx] = sorted_pos;
                ++sorted_pos;
                // Descend right
                size_t right = 2 * f.eytz_idx + 2;
                size_t old_hi = f.hi;
                --sp;
                if (right < n) {
                    ++sp;
                    stack[sp] = {right, sorted_pos, old_hi, 0};
                }
            }
        }
    }

    /// Branchless Eytzinger search. Returns the original sorted index
    /// of the matching element, or size() if not found.
    ///
    /// Uses 1-indexed layout internally for cleaner arithmetic:
    ///   child(i) = 2*i (left) or 2*i+1 (right).
    /// The unconditional descent to a leaf avoids all data-dependent branches.
    [[nodiscard]] auto find(T key) const noexcept -> size_t {
        const auto n = data_.size();
        if (n == 0) [[unlikely]] {
            return n;
        }

        // Branchless descent: unconditionally walk to a leaf.
        // 'candidate' tracks the last node where data_[i] == key.
        size_t i = 0;
        size_t candidate = n; // "not found" sentinel
        while (i < n) {
            // Prefetch both children — unconditional (children may be
            // out-of-bounds but the prefetch is a hint, never faults).
#if defined(_MSC_VER)
            _mm_prefetch(reinterpret_cast<const char *>(data_.data() + 2 * i + 1), _MM_HINT_T0);
#elif defined(__GNUC__) || defined(__clang__)
            __builtin_prefetch(data_.data() + 2 * i + 1, 0, 3);
#endif
            // Record match without branching on it
            if (data_[i] == key) {
                candidate = i;
            }
            // Branchless child selection: left = 2i+1, right = 2i+2
            i = 2 * i + 1 + static_cast<size_t>(data_[i] < key);
        }
        return (candidate < n) ? sorted_index_[candidate] : n;
    }

    [[nodiscard]] auto size() const noexcept -> size_t { return data_.size(); }
    [[nodiscard]] auto empty() const noexcept -> bool { return data_.empty(); }
    [[nodiscard]] auto data() const noexcept -> std::span<const T> { return data_; }
    [[nodiscard]] auto sorted_indices() const noexcept -> std::span<const size_t> { return sorted_index_; }

    [[nodiscard]] auto sorted_keys() const -> std::vector<T> {
        std::vector<T> result(data_.size());
        for (size_t i = 0; i < data_.size(); ++i) {
            result[sorted_index_[i]] = data_[i];
        }
        return result;
    }

  private:
    auto build_recursive(std::span<const T> sorted, size_t eytz_idx, size_t lo, size_t hi) -> size_t {
        if (eytz_idx >= sorted.size() || lo >= hi) {
            return lo;
        }
        lo = build_recursive(sorted, 2 * eytz_idx + 1, lo, hi);
        data_[eytz_idx] = sorted[lo];
        sorted_index_[eytz_idx] = lo;
        ++lo;
        lo = build_recursive(sorted, 2 * eytz_idx + 2, lo, hi);
        return lo;
    }

    std::vector<T> data_;
    std::vector<size_t> sorted_index_;
};

} // namespace smoothie::detail
