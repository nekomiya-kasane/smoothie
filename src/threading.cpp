#include "smoothie/threading.h"

#include <atomic>
#include <cassert>

namespace smoothie {

namespace {
std::atomic<threading_policy> s_policy{default_threading};
std::atomic<bool> s_policy_locked{false};
}  // namespace

auto get_threading_policy() noexcept -> threading_policy {
    return s_policy.load(std::memory_order_acquire);
}

void set_threading_policy(threading_policy policy) {
    assert(!s_policy_locked.load(std::memory_order_acquire) &&
           "set_threading_policy() must be called before any vfs construction");
    s_policy.store(policy, std::memory_order_release);
}

namespace detail {

void lock_threading_policy() noexcept {
    s_policy_locked.store(true, std::memory_order_release);
}

}  // namespace detail

}  // namespace smoothie
