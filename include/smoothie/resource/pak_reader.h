#pragma once

/// @file pak_reader.h
/// @brief Read-only .lpak file parser with MPHF and binary search lookup.

#include "smoothie/detail/eytzinger_array.h"
#include "smoothie/exports.h"
#include "smoothie/resource/compression.h"
#include "smoothie/resource/lpak_format.h"
#include "smoothie/types.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace smoothie::resource {

    /// Read-only view into a .lpak file loaded into memory.
    ///
    /// Validates the header, checksums, and provides access to entries and data.
    class SMOOTHIE_API pak_reader {
      public:
        pak_reader() = default;

        /// Parse a .lpak from a memory buffer. Validates magic, version, and header checksum.
        [[nodiscard]] static auto open(std::span<const std::byte> buffer) -> diagnostic_result<pak_reader>;

        /// Number of entries in the package.
        [[nodiscard]] auto entry_count() const noexcept -> uint32_t;

        /// Access the file header. Returns nullptr if the reader was default-constructed.
        [[nodiscard]] auto header() const noexcept -> const file_header *;

        /// Access the index array.
        [[nodiscard]] auto entries() const noexcept -> std::span<const entry_descriptor>;

        /// Look up an entry by semantic hash. Returns nullptr if not found.
        [[nodiscard]] auto find(uint64_t semantic_hash) const noexcept -> const entry_descriptor *;

        /// Get the raw data for an entry (zero-copy span into the buffer).
        [[nodiscard]] auto data_of(const entry_descriptor &entry) const noexcept -> result<std::span<const std::byte>>;

        /// Get resource data with transparent decompression.
        [[nodiscard]] auto data_of_view(const entry_descriptor &entry) const -> diagnostic_result<resource_view>;

        /// Validate the index checksum.
        [[nodiscard]] auto validate_index_checksum() const noexcept -> bool;

        /// Iterator support for range-based for over entries.
        [[nodiscard]] auto begin() const noexcept -> const entry_descriptor * { return index_.data(); }
        [[nodiscard]] auto end() const noexcept -> const entry_descriptor * { return index_.data() + index_.size(); }

      private:
#if defined(_MSC_VER)
#    pragma warning(push)
#    pragma warning(disable : 4251)
#endif
        std::span<const std::byte> buffer_;
        const file_header *header_ = nullptr;
        std::span<const entry_descriptor> index_;
        std::span<const std::byte> data_payload_;
        ::smoothie::detail::eytzinger_array<uint64_t> compact_hashes_; // O5+O6: Eytzinger layout on 8B hash keys
#if defined(SMOOTHIE_USE_MPHF)
        std::span<const uint32_t> mphf_seeds_;
#endif
#if defined(_MSC_VER)
#    pragma warning(pop)
#endif
    };

} // namespace smoothie::resource
