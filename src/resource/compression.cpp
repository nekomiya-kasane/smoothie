#include "smoothie/resource/compression.h"

#include <format>

#if defined(SMOOTHIE_HAS_COMPRESSION)
#include <lz4.h>
#include <lz4hc.h>
#include <zstd.h>
#endif

namespace smoothie::resource {

auto compress(
    std::span<const std::byte> input,
    compression_mode mode
) -> diagnostic_result<std::vector<std::byte>> {
#if !defined(SMOOTHIE_HAS_COMPRESSION)
    (void)input;
    (void)mode;
    return std::unexpected(error{error_code::invalid_argument, "compression support not compiled"});
#else
    if (input.empty()) {
        return std::vector<std::byte>{};
    }

    switch (mode) {
    case compression_mode::lz4: {
        const auto src_size = static_cast<int>(input.size());
        const auto max_dst = LZ4_compressBound(src_size);
        std::vector<std::byte> out(static_cast<size_t>(max_dst));

        const auto compressed_size = LZ4_compress_HC(
            reinterpret_cast<const char*>(input.data()),
            reinterpret_cast<char*>(out.data()),
            src_size,
            max_dst,
            LZ4HC_CLEVEL_DEFAULT
        );

        if (compressed_size <= 0) {
            return std::unexpected(error{error_code::io_error, "LZ4 compression failed"});
        }

        out.resize(static_cast<size_t>(compressed_size));
        return out;
    }

    case compression_mode::zstd: {
        const auto max_dst = ZSTD_compressBound(input.size());
        std::vector<std::byte> out(max_dst);

        const auto compressed_size = ZSTD_compress(
            out.data(), max_dst,
            input.data(), input.size(),
            ZSTD_defaultCLevel()
        );

        if (ZSTD_isError(compressed_size)) {
            return std::unexpected(error{error_code::io_error,
                std::format("Zstd compression failed: {}", ZSTD_getErrorName(compressed_size))});
        }

        out.resize(compressed_size);
        return out;
    }

    case compression_mode::none:
        return std::unexpected(error{error_code::invalid_argument,
            "compression_mode::none passed to compress()"});
    }

    return std::unexpected(error{error_code::invalid_argument, "unknown compression mode"});
#endif
}

auto decompress(
    std::span<const std::byte> compressed,
    uint32_t uncompressed_size,
    entry_flags flags
) -> diagnostic_result<std::vector<std::byte>> {
#if !defined(SMOOTHIE_HAS_COMPRESSION)
    (void)compressed;
    (void)uncompressed_size;
    (void)flags;
    return std::unexpected(error{error_code::invalid_argument, "compression support not compiled"});
#else
    if (compressed.empty() || uncompressed_size == 0) {
        return std::vector<std::byte>{};
    }

    static constexpr uint32_t max_decompress_size = 256u * 1024u * 1024u;
    if (uncompressed_size > max_decompress_size) {
        return std::unexpected(error{error_code::invalid_argument,
            std::format("decompression size {} exceeds limit of {} bytes",
                uncompressed_size, max_decompress_size)});
    }

    std::vector<std::byte> out(uncompressed_size);

    if (has_flag(flags, entry_flags::lz4)) {
        const auto result = LZ4_decompress_safe(
            reinterpret_cast<const char*>(compressed.data()),
            reinterpret_cast<char*>(out.data()),
            static_cast<int>(compressed.size()),
            static_cast<int>(uncompressed_size)
        );

        if (result < 0) {
            return std::unexpected(error{error_code::corrupted,
                std::format("LZ4 decompression failed (error {})", result)});
        }

        if (static_cast<uint32_t>(result) != uncompressed_size) {
            return std::unexpected(error{error_code::corrupted,
                std::format("LZ4 decompressed {} bytes, expected {}", result, uncompressed_size)});
        }

        return out;
    }

    if (has_flag(flags, entry_flags::zstd)) {
        const auto result = ZSTD_decompress(
            out.data(), uncompressed_size,
            compressed.data(), compressed.size()
        );

        if (ZSTD_isError(result)) {
            return std::unexpected(error{error_code::corrupted,
                std::format("Zstd decompression failed: {}", ZSTD_getErrorName(result))});
        }

        if (result != static_cast<size_t>(uncompressed_size)) {
            return std::unexpected(error{error_code::corrupted,
                std::format("Zstd decompressed {} bytes, expected {}", result, uncompressed_size)});
        }

        return out;
    }

    return std::unexpected(error{error_code::invalid_argument,
        "compressed flag set but no known codec flag (lz4/zstd)"});
#endif
}

auto decompress_reuse(
    std::span<const std::byte> compressed,
    uint32_t uncompressed_size,
    entry_flags flags,
    std::vector<std::byte>& output
) -> diagnostic_result<void> {
#if !defined(SMOOTHIE_HAS_COMPRESSION)
    (void)compressed;
    (void)uncompressed_size;
    (void)flags;
    (void)output;
    return std::unexpected(error{error_code::invalid_argument, "compression support not compiled"});
#else
    if (compressed.empty() || uncompressed_size == 0) {
        output.clear();
        return {};
    }

    static constexpr uint32_t max_decompress_size = 256u * 1024u * 1024u;
    if (uncompressed_size > max_decompress_size) {
        return std::unexpected(error{error_code::invalid_argument,
            std::format("decompression size {} exceeds limit of {} bytes",
                uncompressed_size, max_decompress_size)});
    }

    output.resize(uncompressed_size);

    auto r = decompress_into(compressed, std::span<std::byte>(output), flags);
    if (!r.has_value()) {
        return std::unexpected(r.error());
    }

    if (*r != uncompressed_size) {
        return std::unexpected(error{error_code::corrupted,
            std::format("decompressed {} bytes, expected {}", *r, uncompressed_size)});
    }

    return {};
#endif
}

auto decompress_into(
    std::span<const std::byte> compressed,
    std::span<std::byte> output,
    entry_flags flags
) -> diagnostic_result<size_t> {
#if !defined(SMOOTHIE_HAS_COMPRESSION)
    (void)compressed;
    (void)output;
    (void)flags;
    return std::unexpected(error{error_code::invalid_argument, "compression support not compiled"});
#else
    if (compressed.empty() || output.empty()) {
        return size_t{0};
    }

    if (has_flag(flags, entry_flags::lz4)) {
        const auto result = LZ4_decompress_safe(
            reinterpret_cast<const char*>(compressed.data()),
            reinterpret_cast<char*>(output.data()),
            static_cast<int>(compressed.size()),
            static_cast<int>(output.size())
        );

        if (result < 0) {
            return std::unexpected(error{error_code::corrupted,
                std::format("LZ4 decompression failed (error {})", result)});
        }

        return static_cast<size_t>(result);
    }

    if (has_flag(flags, entry_flags::zstd)) {
        const auto result = ZSTD_decompress(
            output.data(), output.size(),
            compressed.data(), compressed.size()
        );

        if (ZSTD_isError(result)) {
            return std::unexpected(error{error_code::corrupted,
                std::format("Zstd decompression failed: {}", ZSTD_getErrorName(result))});
        }

        return result;
    }

    return std::unexpected(error{error_code::invalid_argument,
        "compressed flag set but no known codec flag (lz4/zstd)"});
#endif
}

}  // namespace smoothie::resource
