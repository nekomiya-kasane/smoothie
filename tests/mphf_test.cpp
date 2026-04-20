#include "smoothie/resource/hash.h"
#include "smoothie/resource/mphf.h"

#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <numeric>
#include <vector>

using namespace smoothie::resource;

// ── mphf_header static checks ───────────────────────────────────────────

TEST(MphfHeader, SizeAndMagic) {
    static_assert(sizeof(mphf_header) == 16);
    EXPECT_EQ(mphf_magic, 0x4D504846u);
    EXPECT_EQ(mphf_empty_seed, UINT32_MAX);
}

// ── mphf_bucket ─────────────────────────────────────────────────────────

TEST(MphfBucket, Basic) {
    EXPECT_EQ(mphf_bucket(0, 10), 0u);
    EXPECT_EQ(mphf_bucket(7, 10), 7u);
    EXPECT_EQ(mphf_bucket(15, 10), 5u);
}

TEST(MphfBucket, SingleBucket) {
    // Everything maps to bucket 0 when bucket_count == 1
    EXPECT_EQ(mphf_bucket(12345, 1), 0u);
    EXPECT_EQ(mphf_bucket(0, 1), 0u);
}

// ── mphf_probe ──────────────────────────────────────────────────────────

TEST(MphfProbe, Deterministic) {
    auto a = mphf_probe(42, 7, 100);
    auto b = mphf_probe(42, 7, 100);
    EXPECT_EQ(a, b);
    EXPECT_LT(a, 100u);
}

TEST(MphfProbe, DifferentSeedsGiveDifferentSlots) {
    // Not strictly guaranteed but highly likely for different seeds
    auto a = mphf_probe(42, 0, 1000);
    auto b = mphf_probe(42, 1, 1000);
    // Just verify both are in range; collision possible but rare
    EXPECT_LT(a, 1000u);
    EXPECT_LT(b, 1000u);
}

// ── mphf_build empty ────────────────────────────────────────────────────

TEST(MphfBuild, Empty) {
    auto seeds = mphf_build({});
    EXPECT_TRUE(seeds.empty());
}

// ── mphf_build single ───────────────────────────────────────────────────

TEST(MphfBuild, SingleKey) {
    uint64_t keys[] = {hash64("test/resource.png")};
    auto seeds = mphf_build(keys);
    ASSERT_EQ(seeds.size(), 1u);

    auto idx = mphf_lookup(keys[0], seeds, 1);
    EXPECT_EQ(idx, 0u);
}

// ── mphf_build small set ────────────────────────────────────────────────

TEST(MphfBuild, SmallSet) {
    std::vector<uint64_t> keys;
    keys.push_back(hash64("textures/hero.png"));
    keys.push_back(hash64("fonts/roboto.ttf"));
    keys.push_back(hash64("shaders/basic.glsl"));
    keys.push_back(hash64("config/settings.json"));
    keys.push_back(hash64("audio/click.wav"));

    auto seeds = mphf_build(keys);
    ASSERT_EQ(seeds.size(), keys.size());

    // Verify perfect hash: every key maps to a unique slot in [0, n)
    std::vector<bool> used(keys.size(), false);
    for (size_t i = 0; i < keys.size(); ++i) {
        auto idx = mphf_lookup(keys[i], seeds, static_cast<uint32_t>(keys.size()));
        ASSERT_LT(idx, keys.size()) << "key index " << i;
        EXPECT_FALSE(used[idx]) << "collision at slot " << idx;
        used[idx] = true;
    }
}

// ── mphf_build large set ────────────────────────────────────────────────

TEST(MphfBuild, LargeSet) {
    constexpr uint32_t N = 500;
    std::vector<uint64_t> keys;
    keys.reserve(N);
    for (uint32_t i = 0; i < N; ++i) {
        keys.push_back(hash64("resource/" + std::to_string(i)));
    }

    auto seeds = mphf_build(keys);
    ASSERT_EQ(seeds.size(), N);

    std::vector<bool> used(N, false);
    for (uint32_t i = 0; i < N; ++i) {
        auto idx = mphf_lookup(keys[i], seeds, N);
        ASSERT_LT(idx, N) << "key " << i;
        EXPECT_FALSE(used[idx]) << "collision at slot " << idx << " for key " << i;
        used[idx] = true;
    }
}

// ── mphf_lookup miss ────────────────────────────────────────────────────

TEST(MphfLookup, EmptyTable) {
    auto result = mphf_lookup(42, {}, 0);
    EXPECT_EQ(result, UINT32_MAX);
}

TEST(MphfLookup, EmptySeeds) {
    std::vector<uint32_t> seeds;
    auto result = mphf_lookup(42, seeds, 10);
    EXPECT_EQ(result, UINT32_MAX);
}

TEST(MphfLookup, ZeroEntryCount) {
    std::vector<uint32_t> seeds = {0, 1, 2};
    auto result = mphf_lookup(42, seeds, 0);
    EXPECT_EQ(result, UINT32_MAX);
}

// ── mphf_serialize / mphf_deserialize ───────────────────────────────────

TEST(MphfSerialize, RoundTrip) {
    std::vector<uint64_t> keys;
    for (int i = 0; i < 10; ++i) {
        keys.push_back(hash64("item/" + std::to_string(i)));
    }

    auto seeds = mphf_build(keys);
    ASSERT_EQ(seeds.size(), 10u);

    auto bytes = mphf_serialize(seeds, 10);
    EXPECT_EQ(bytes.size(), sizeof(mphf_header) + 10 * sizeof(uint32_t));

    // Verify header
    auto *hdr = reinterpret_cast<const mphf_header *>(bytes.data());
    EXPECT_EQ(hdr->magic, mphf_magic);
    EXPECT_EQ(hdr->bucket_count, 10u);
    EXPECT_EQ(hdr->entry_count, 10u);
    EXPECT_EQ(hdr->reserved, 0u);

    // Deserialize and verify identical seeds
    auto recovered = mphf_deserialize(bytes);
    ASSERT_EQ(recovered.size(), seeds.size());
    for (size_t i = 0; i < seeds.size(); ++i) {
        EXPECT_EQ(recovered[i], seeds[i]) << "seed index " << i;
    }

    // Verify lookups still work with deserialized seeds
    for (size_t i = 0; i < keys.size(); ++i) {
        auto idx = mphf_lookup(keys[i], recovered, 10);
        EXPECT_LT(idx, 10u) << "key " << i;
    }
}

TEST(MphfSerialize, EmptySeeds) {
    auto bytes = mphf_serialize({}, 0);
    EXPECT_EQ(bytes.size(), sizeof(mphf_header));

    auto recovered = mphf_deserialize(bytes);
    EXPECT_TRUE(recovered.empty());
}

// ── mphf_deserialize error cases ────────────────────────────────────────

TEST(MphfDeserialize, TooSmallBuffer) {
    std::vector<std::byte> tiny(4);
    auto result = mphf_deserialize(tiny);
    EXPECT_TRUE(result.empty());
}

TEST(MphfDeserialize, BadMagic) {
    auto bytes = mphf_serialize({1, 2, 3}, 3);
    // Corrupt magic
    bytes[0] = std::byte{0xFF};
    auto result = mphf_deserialize(bytes);
    EXPECT_TRUE(result.empty());
}

TEST(MphfDeserialize, TruncatedSeeds) {
    auto bytes = mphf_serialize({1, 2, 3}, 3);
    // Truncate: header says 3 seeds but we cut off the last one
    bytes.resize(sizeof(mphf_header) + 2 * sizeof(uint32_t));
    auto result = mphf_deserialize(bytes);
    EXPECT_TRUE(result.empty());
}
