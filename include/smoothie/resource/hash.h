#pragma once

/// @file hash.h
/// @brief FNV-1a hash functions (32-bit and 64-bit) for resource and message key hashing.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace smoothie::resource {

// ── FNV-1a 64-bit hash (default, no external dependency) ────────────────

namespace detail {

inline constexpr uint64_t fnv1a_offset_basis = 14695981039346656037ULL;
inline constexpr uint64_t fnv1a_prime = 1099511628211ULL;

} // namespace detail

/// Compile-time FNV-1a 64-bit hash of a string.
[[nodiscard]] constexpr auto hash64(std::string_view s) noexcept -> uint64_t {
    uint64_t h = detail::fnv1a_offset_basis;
    for (char c : s) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        h *= detail::fnv1a_prime;
    }
    return h;
}

/// FNV-1a 64-bit hash of a byte span.
[[nodiscard]] constexpr auto hash64(std::span<const std::byte> data) noexcept -> uint64_t {
    uint64_t h = detail::fnv1a_offset_basis;
    for (auto b : data) {
        h ^= static_cast<uint64_t>(b);
        h *= detail::fnv1a_prime;
    }
    return h;
}

/// Namespace-aware hash: hash64(ns + "/" + uri).
[[nodiscard]] constexpr auto hash64_ns(std::string_view ns, std::string_view uri) noexcept -> uint64_t {
    uint64_t h = detail::fnv1a_offset_basis;
    for (char c : ns) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        h *= detail::fnv1a_prime;
    }
    h ^= static_cast<uint64_t>(static_cast<unsigned char>('/'));
    h *= detail::fnv1a_prime;
    for (char c : uri) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        h *= detail::fnv1a_prime;
    }
    return h;
}

/// FNV-1a 32-bit hash (for smaller indices).
[[nodiscard]] constexpr auto hash32(std::string_view s) noexcept -> uint32_t {
    uint32_t h = 2166136261u;
    for (char c : s) {
        h ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
        h *= 16777619u;
    }
    return h;
}

/// User-defined literal for compile-time FNV-1a 64-bit hash.
/// Usage: auto h = "path/to/resource"_h64;
[[nodiscard]] consteval auto operator""_h64(const char *s, size_t len) noexcept -> uint64_t {
    uint64_t h = detail::fnv1a_offset_basis;
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(s[i]));
        h *= detail::fnv1a_prime;
    }
    return h;
}

} // namespace smoothie::resource
