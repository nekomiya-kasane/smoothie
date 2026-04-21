#pragma once

/// @file threading.h
/// @brief Runtime-optional thread safety policy for smoothie.

#include "smoothie/exports.h"

#include <cstdint>

namespace smoothie {

    /// Thread safety strategy (compile-time default + runtime switchable).
    enum class threading_policy : uint8_t {
        single_thread = 0, ///< Zero atomics, zero locks, max performance.
        multi_thread = 1,  ///< RCU + atomic<shared_ptr>, read path lock-free.
    };

/// Compile-time default policy (CMake: SMOOTHIE_DEFAULT_SINGLE_THREAD).
#if defined(SMOOTHIE_SINGLE_THREAD)
    inline constexpr threading_policy default_threading = threading_policy::single_thread;
#else
    inline constexpr threading_policy default_threading = threading_policy::multi_thread;
#endif

    /// Query the active threading policy.
    [[nodiscard]] SMOOTHIE_API auto get_threading_policy() noexcept -> threading_policy;

    /// Set the threading policy. Must be called before any vfs construction.
    SMOOTHIE_API void set_threading_policy(threading_policy policy);

} // namespace smoothie
