#pragma once

/// @file stats.h
/// @brief VFS performance counters for smoothie.

#include "smoothie/exports.h"

#include <atomic>
#include <cstdint>

namespace smoothie {

    // ── Conditional compilation macros ──────────────────────────────────

#ifdef SMOOTHIE_ENABLE_STATS
#define SMOOTHIE_STAT_INC(counter) (++(counter))
#define SMOOTHIE_STAT_ADD(counter, n) ((counter) += (n))
#else
#define SMOOTHIE_STAT_INC(counter) ((void)0)
#define SMOOTHIE_STAT_ADD(counter, n) ((void)0)
#endif

    // ── VFS performance counters ────────────────────────────────────────

    struct vfs_stats {
        uint64_t get_count = 0;
        uint64_t hit_count = 0;
        uint64_t miss_count = 0;
        uint64_t mount_count = 0;
        uint64_t total_bytes = 0;
    };

    // ── Atomic counter block (used internally) ──────────────────────────

#ifdef SMOOTHIE_ENABLE_STATS
    struct atomic_vfs_counters {
        std::atomic<uint64_t> get_count{0};
        std::atomic<uint64_t> hit_count{0};
        std::atomic<uint64_t> miss_count{0};
        std::atomic<uint64_t> mount_count{0};
        std::atomic<uint64_t> total_bytes{0};

        [[nodiscard]] auto snapshot() const noexcept -> vfs_stats {
            return {
                .get_count = get_count.load(std::memory_order_relaxed),
                .hit_count = hit_count.load(std::memory_order_relaxed),
                .miss_count = miss_count.load(std::memory_order_relaxed),
                .mount_count = mount_count.load(std::memory_order_relaxed),
                .total_bytes = total_bytes.load(std::memory_order_relaxed),
            };
        }

        void reset() noexcept {
            get_count.store(0, std::memory_order_relaxed);
            hit_count.store(0, std::memory_order_relaxed);
            miss_count.store(0, std::memory_order_relaxed);
            mount_count.store(0, std::memory_order_relaxed);
            total_bytes.store(0, std::memory_order_relaxed);
        }
    };
#endif

} // namespace smoothie
