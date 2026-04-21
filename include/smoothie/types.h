#pragma once

/// @file types.h
/// @brief Core types: error codes, result types, and resource_view.

#include "smoothie/exports.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace smoothie {

    // ── Error handling ──────────────────────────────────────────────────────

    /// Error codes for hot-path operations (zero allocation).
    enum class error_code : uint8_t {
        ok,
        not_found,
        corrupted,
        mmap_failed,
        version_mismatch,
        already_mounted,
        not_mounted,
        io_error,
        invalid_argument,
    };

    /// Convert error_code to string_view.
    [[nodiscard]] constexpr auto to_string_view(error_code ec) noexcept -> std::string_view {
        switch (ec) {
        case error_code::ok:
            return "ok";
        case error_code::not_found:
            return "not_found";
        case error_code::corrupted:
            return "corrupted";
        case error_code::mmap_failed:
            return "mmap_failed";
        case error_code::version_mismatch:
            return "version_mismatch";
        case error_code::already_mounted:
            return "already_mounted";
        case error_code::not_mounted:
            return "not_mounted";
        case error_code::io_error:
            return "io_error";
        case error_code::invalid_argument:
            return "invalid_argument";
        default:
            return "unknown";
        }
    }

    /// Hot-path result type (zero allocation).
    template <typename T> using result = std::expected<T, error_code>;

    /// Diagnostic error with human-readable message (for low-frequency operations).
    struct error {
        error_code code;
        std::string message;
    };

    /// Diagnostic result type (for init/mount and other low-frequency operations).
    template <typename T> using diagnostic_result = std::expected<T, error>;

    // ── Resource view ───────────────────────────────────────────────────────

    /// Unified resource view: zero-copy span (res://) or owned buffer (file://, compressed).
    ///
    /// For res:// paths, holds a span pointing directly into mmap memory (zero-copy).
    /// For file:// paths or compressed resources, holds an owned std::vector<std::byte>.
    class SMOOTHIE_API resource_view {
      public:
        /// Zero-copy construction (res:// mmap data).
        explicit resource_view(std::span<const std::byte> borrowed) noexcept;

        /// Owned construction (file:// read or decompressed data).
        explicit resource_view(std::vector<std::byte> owned) noexcept;

        resource_view(resource_view &&) noexcept = default;
        resource_view &operator=(resource_view &&) noexcept = default;

        /// Copy construction: allowed for borrowed (zero-copy) views only.
        /// Owned views cannot be copied (use move instead).
        resource_view(const resource_view &other);
        resource_view &operator=(const resource_view &other);

        /// Unified access — always returns a span regardless of storage mode.
        [[nodiscard]] auto data() const noexcept -> std::span<const std::byte>;

        /// Size in bytes.
        [[nodiscard]] auto size() const noexcept -> size_t;

        /// True if this view is empty (zero bytes).
        [[nodiscard]] auto empty() const noexcept -> bool;

        /// True if this view owns its buffer (file:// or decompressed).
        [[nodiscard]] auto is_owned() const noexcept -> bool;

        /// Zero-copy conversion to string_view. Interprets bytes as UTF-8 text.
        [[nodiscard]] auto as_string_view() const noexcept -> std::string_view;

        /// Copy bytes into a std::string. Interprets bytes as UTF-8 text.
        [[nodiscard]] auto as_string() const -> std::string;

        /// Return a sub-range view (borrowed, zero-copy).
        [[nodiscard]] auto subspan(size_t offset, size_t count = std::dynamic_extent) const
            -> std::span<const std::byte>;

      private:
#if defined(_MSC_VER)
#    pragma warning(push)
#    pragma warning(disable : 4251)
#endif
        std::variant<std::span<const std::byte>, std::vector<std::byte>> storage_;
#if defined(_MSC_VER)
#    pragma warning(pop)
#endif
        std::span<const std::byte> cached_data_;
    };

} // namespace smoothie

/// std::formatter specialization for smoothie::error_code.
template <> struct std::formatter<smoothie::error_code> : std::formatter<std::string_view> {
    auto format(smoothie::error_code ec, std::format_context &ctx) const {
        return std::formatter<std::string_view>::format(smoothie::to_string_view(ec), ctx);
    }
};
