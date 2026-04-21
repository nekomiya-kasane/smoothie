#pragma once

/// @file detail/snapshot_holder.h
/// @brief Policy-based snapshot holder for runtime-optional thread safety.

#include "smoothie/threading.h"

#include <atomic>
#include <cassert>
#include <memory>
#include <variant>

namespace smoothie::detail {

    /// Lock the threading policy (called once by first vfs construction).
    void lock_threading_policy() noexcept;

    /// Single-thread snapshot holder: raw pointer, zero overhead.
    template <typename T> struct snapshot_holder_st {
        auto load() const noexcept -> std::shared_ptr<const T> { return snap_; }
        void store(std::shared_ptr<const T> p) noexcept { snap_ = std::move(p); }

      private:
        std::shared_ptr<const T> snap_;
    };

    /// Multi-thread snapshot holder: atomic shared_ptr, RCU semantics.
    ///
    /// C++20 std::atomic<shared_ptr> is preferred (lock-free on MSVC STL / libstdc++).
    /// libc++ has not yet implemented the specialization (__cpp_lib_atomic_shared_ptr
    /// is commented out as of LLVM 21), so we fall back to the deprecated free
    /// functions std::atomic_load_explicit / std::atomic_store_explicit which libc++
    /// implements via a global mutex table.
    template <typename T> struct snapshot_holder_mt {
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
        auto load() const noexcept -> std::shared_ptr<const T> { return snap_.load(std::memory_order_acquire); }
        void store(std::shared_ptr<const T> p) noexcept { snap_.store(std::move(p), std::memory_order_release); }

      private:
        std::atomic<std::shared_ptr<const T>> snap_;
#else
        auto load() const noexcept -> std::shared_ptr<const T> {
            return std::atomic_load_explicit(&snap_, std::memory_order_acquire);
        }
        void store(std::shared_ptr<const T> p) noexcept {
            std::atomic_store_explicit(&snap_, std::move(p), std::memory_order_release);
        }

      private:
        std::shared_ptr<const T> snap_;
#endif
    };

    /// Type-erased snapshot holder that dispatches based on runtime policy.
    /// Uses a vtable-based approach instead of std::variant because
    /// std::atomic<shared_ptr> is neither copyable nor movable.
    template <typename T> class snapshot_holder {
      public:
        explicit snapshot_holder(threading_policy policy = get_threading_policy()) : policy_(policy) {
            lock_threading_policy();
            if (policy_ == threading_policy::single_thread) {
                new (&st_) snapshot_holder_st<T>{};
            } else {
                new (&mt_) snapshot_holder_mt<T>{};
            }
        }

        ~snapshot_holder() {
            if (policy_ == threading_policy::single_thread) {
                st_.~snapshot_holder_st();
            } else {
                mt_.~snapshot_holder_mt();
            }
        }

        snapshot_holder(const snapshot_holder &) = delete;
        snapshot_holder &operator=(const snapshot_holder &) = delete;

        [[nodiscard]] auto load() const noexcept -> std::shared_ptr<const T> {
            if (policy_ == threading_policy::single_thread) {
                return st_.load();
            }
            return mt_.load();
        }

        void store(std::shared_ptr<const T> p) noexcept {
            if (policy_ == threading_policy::single_thread) {
                st_.store(std::move(p));
            } else {
                mt_.store(std::move(p));
            }
        }

        [[nodiscard]] auto policy() const noexcept -> threading_policy { return policy_; }

      private:
        threading_policy policy_;
        union {
            snapshot_holder_st<T> st_;
            snapshot_holder_mt<T> mt_;
        };
    };

} // namespace smoothie::detail
