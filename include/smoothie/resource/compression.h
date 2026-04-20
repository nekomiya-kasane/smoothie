#pragma once

/// @file compression.h
/// @brief Block compression/decompression for .lpak entries (LZ4, Zstd).

#include "smoothie/exports.h"
#include "smoothie/resource/lpak_format.h"
#include "smoothie/types.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace smoothie::resource {

/// Compression codec selection.
enum class compression_mode : uint8_t {
    none = 0,
    lz4 = 1,
    zstd = 2,
};

/// @brief Compress a block of data using the specified codec.
[[nodiscard]] SMOOTHIE_API auto compress(std::span<const std::byte> input, compression_mode mode)
    -> diagnostic_result<std::vector<std::byte>>;

/// @brief Decompress a block of data using the codec indicated by entry flags.
[[nodiscard]] SMOOTHIE_API auto decompress(std::span<const std::byte> compressed, uint32_t uncompressed_size,
                                           entry_flags flags) -> diagnostic_result<std::vector<std::byte>>;

/// @brief Decompress into a pre-allocated output buffer (zero-allocation hot path).
/// @param output  must be at least uncompressed_size bytes.
/// @return number of bytes written, or error.
[[nodiscard]] SMOOTHIE_API auto decompress_into(std::span<const std::byte> compressed, std::span<std::byte> output,
                                                entry_flags flags) -> diagnostic_result<size_t>;

/// @brief Decompress into a reusable vector (amortized-zero-allocation hot path).
/// The vector is resized to uncompressed_size; if its capacity already suffices,
/// no heap allocation occurs.
[[nodiscard]] SMOOTHIE_API auto decompress_reuse(std::span<const std::byte> compressed, uint32_t uncompressed_size,
                                                 entry_flags flags, std::vector<std::byte> &output)
    -> diagnostic_result<void>;

/// @brief Map compression_mode to the corresponding entry_flags bits.
[[nodiscard]] constexpr auto compression_to_flags(compression_mode mode) noexcept -> entry_flags {
    switch (mode) {
    case compression_mode::lz4:
        return entry_flags::compressed | entry_flags::lz4;
    case compression_mode::zstd:
        return entry_flags::compressed | entry_flags::zstd;
    default:
        return entry_flags::none;
    }
}

/// @brief Check if entry flags indicate the data is compressed.
[[nodiscard]] constexpr auto is_compressed(entry_flags f) noexcept -> bool {
    return has_flag(f, entry_flags::compressed);
}

// ── Streaming decompression ──────────────────────────────────────────

/// @brief Callback invoked for each decompressed chunk.
/// @param data  pointer to decompressed bytes
/// @param size  number of bytes in this chunk
/// Return false to abort the stream early.
using decompress_chunk_callback = std::function<bool(const std::byte *data, size_t size)>;

/// @brief Streaming decompressor for processing large resources in chunks.
///
/// Usage:
/// @code
///   decompression_stream ds(entry_flags::compressed | entry_flags::zstd, 1024*1024);
///   ds.feed(chunk1);
///   ds.feed(chunk2);
///   auto result = ds.finish([](const std::byte* data, size_t n) {
///       file.write(data, n);
///       return true;
///   });
/// @endcode
class SMOOTHIE_API decompression_stream {
  public:
    /// @brief Create a streaming decompressor.
    /// @param flags            entry_flags indicating the codec (lz4 or zstd)
    /// @param uncompressed_size total expected uncompressed size (0 = unknown, Zstd only)
    /// @param chunk_size       output buffer size per callback invocation (default 64 KB)
    explicit decompression_stream(entry_flags flags, uint32_t uncompressed_size = 0, size_t chunk_size = 65536);

    ~decompression_stream();

    decompression_stream(const decompression_stream &) = delete;
    decompression_stream &operator=(const decompression_stream &) = delete;
    decompression_stream(decompression_stream &&) noexcept;
    decompression_stream &operator=(decompression_stream &&) noexcept;

    /// @brief Feed compressed data into the stream.
    /// Decompressed output is buffered internally.
    [[nodiscard]] auto feed(std::span<const std::byte> compressed_chunk) -> result<void>;

    /// @brief Finalize the stream and flush all remaining decompressed data via callback.
    /// @param cb  callback invoked for each decompressed chunk
    [[nodiscard]] auto finish(decompress_chunk_callback cb) -> diagnostic_result<uint64_t>;

    /// @brief Total bytes decompressed so far.
    [[nodiscard]] auto bytes_produced() const noexcept -> uint64_t;

  private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace smoothie::resource
