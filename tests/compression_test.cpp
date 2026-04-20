#include "smoothie/resource/compression.h"
#include "smoothie/resource/lpak_format.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <numeric>
#include <string>
#include <vector>

using namespace smoothie::resource;

// Helper: make a byte vector from a string
static auto to_bytes(std::string_view s) -> std::vector<std::byte> {
    std::vector<std::byte> v(s.size());
    std::memcpy(v.data(), s.data(), s.size());
    return v;
}

// Helper: make a repeating byte pattern (compressible)
static auto make_compressible(size_t size) -> std::vector<std::byte> {
    std::vector<std::byte> v(size);
    for (size_t i = 0; i < size; ++i) v[i] = static_cast<std::byte>(i % 64);
    return v;
}

// ── compress + decompress round-trip ─────────────────────────────────────

TEST(Compression, LZ4RoundTrip) {
    auto original = make_compressible(4096);
    auto compressed = compress(original, compression_mode::lz4);
    ASSERT_TRUE(compressed.has_value()) << compressed.error().message;
    EXPECT_LT(compressed->size(), original.size());

    auto flags = entry_flags::compressed | entry_flags::lz4;
    auto decompressed = decompress(*compressed, static_cast<uint32_t>(original.size()), flags);
    ASSERT_TRUE(decompressed.has_value()) << decompressed.error().message;
    EXPECT_EQ(*decompressed, original);
}

TEST(Compression, ZstdRoundTrip) {
    auto original = make_compressible(4096);
    auto compressed = compress(original, compression_mode::zstd);
    ASSERT_TRUE(compressed.has_value()) << compressed.error().message;
    EXPECT_LT(compressed->size(), original.size());

    auto flags = entry_flags::compressed | entry_flags::zstd;
    auto decompressed = decompress(*compressed, static_cast<uint32_t>(original.size()), flags);
    ASSERT_TRUE(decompressed.has_value()) << decompressed.error().message;
    EXPECT_EQ(*decompressed, original);
}

TEST(Compression, NoneReturnsError) {
    auto original = to_bytes("hello world");
    auto compressed = compress(original, compression_mode::none);
    EXPECT_FALSE(compressed.has_value());
    EXPECT_EQ(compressed.error().code, smoothie::error_code::invalid_argument);
}

// ── decompress_into (zero-allocation hot path) ──────────────────────────

TEST(Compression, DecompressIntoLZ4) {
    auto original = make_compressible(8192);
    auto compressed = compress(original, compression_mode::lz4);
    ASSERT_TRUE(compressed.has_value());

    std::vector<std::byte> output(original.size());
    auto flags = entry_flags::compressed | entry_flags::lz4;
    auto result = decompress_into(*compressed, output, flags);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(*result, original.size());
    EXPECT_EQ(output, original);
}

TEST(Compression, DecompressIntoZstd) {
    auto original = make_compressible(8192);
    auto compressed = compress(original, compression_mode::zstd);
    ASSERT_TRUE(compressed.has_value());

    std::vector<std::byte> output(original.size());
    auto flags = entry_flags::compressed | entry_flags::zstd;
    auto result = decompress_into(*compressed, output, flags);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(*result, original.size());
    EXPECT_EQ(output, original);
}

TEST(Compression, DecompressIntoNoCodecReturnsError) {
    auto original = to_bytes("raw data no compression");
    std::vector<std::byte> output(original.size());
    // compressed flag set but no codec => error
    auto result = decompress_into(original, output, entry_flags::compressed);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, smoothie::error_code::invalid_argument);
}

TEST(Compression, DecompressIntoEmptyInput) {
    std::vector<std::byte> output(16);
    auto flags = entry_flags::compressed | entry_flags::lz4;
    auto result = decompress_into({}, output, flags);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0u);
}

TEST(Compression, DecompressEmptyInput) {
    auto result = decompress({}, 0, entry_flags::compressed | entry_flags::lz4);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST(Compression, DecompressReuseEmptyInput) {
    std::vector<std::byte> buf;
    auto result = decompress_reuse({}, 0, entry_flags::compressed | entry_flags::lz4, buf);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(buf.empty());
}

// ── decompress_reuse (amortized-zero-allocation) ────────────────────────

TEST(Compression, DecompressReuseLZ4) {
    auto original = make_compressible(4096);
    auto compressed = compress(original, compression_mode::lz4);
    ASSERT_TRUE(compressed.has_value());

    std::vector<std::byte> reuse_buf;
    auto flags = entry_flags::compressed | entry_flags::lz4;
    auto result = decompress_reuse(*compressed, static_cast<uint32_t>(original.size()), flags, reuse_buf);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(reuse_buf, original);

    // Second call should reuse capacity (no allocation)
    auto cap_before = reuse_buf.capacity();
    result = decompress_reuse(*compressed, static_cast<uint32_t>(original.size()), flags, reuse_buf);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(reuse_buf.capacity(), cap_before);
    EXPECT_EQ(reuse_buf, original);
}

TEST(Compression, DecompressReuseZstd) {
    auto original = make_compressible(4096);
    auto compressed = compress(original, compression_mode::zstd);
    ASSERT_TRUE(compressed.has_value());

    std::vector<std::byte> reuse_buf;
    auto flags = entry_flags::compressed | entry_flags::zstd;
    auto result = decompress_reuse(*compressed, static_cast<uint32_t>(original.size()), flags, reuse_buf);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(reuse_buf, original);
}

// ── Edge cases ──────────────────────────────────────────────────────────

TEST(Compression, SmallData) {
    auto original = to_bytes("x");
    auto compressed = compress(original, compression_mode::lz4);
    ASSERT_TRUE(compressed.has_value());

    auto flags = entry_flags::compressed | entry_flags::lz4;
    auto decompressed = decompress(*compressed, 1, flags);
    ASSERT_TRUE(decompressed.has_value());
    EXPECT_EQ(*decompressed, original);
}

TEST(Compression, CompressionToFlags) {
    EXPECT_EQ(compression_to_flags(compression_mode::none), entry_flags::none);
    EXPECT_EQ(compression_to_flags(compression_mode::lz4), entry_flags::compressed | entry_flags::lz4);
    EXPECT_EQ(compression_to_flags(compression_mode::zstd), entry_flags::compressed | entry_flags::zstd);
}

TEST(Compression, IsCompressed) {
    EXPECT_FALSE(is_compressed(entry_flags::none));
    EXPECT_TRUE(is_compressed(entry_flags::compressed));
    EXPECT_TRUE(is_compressed(entry_flags::compressed | entry_flags::lz4));
    EXPECT_TRUE(is_compressed(entry_flags::compressed | entry_flags::zstd));
}
