#pragma once

/// Minimal Perfect Hash Function (MPHF) for O(1) resource lookup.
///
/// Uses a two-level CHD (Compress, Hash, Displace) scheme:
///   Level 1: hash -> bucket (via modulo)
///   Level 2: bucket -> (seed) -> final index (via seeded hash)
///
/// The MPHF table is a flat array of uint32_t seeds, one per bucket.
/// Build time: O(n), lookup time: O(1), space: ~4 bytes per entry.
///
/// Controlled by SMOOTHIE_USE_MPHF macro:
///   - When defined: pak_reader::find() and vfs::get() use O(1) MPHF lookup
///   - When not defined (default): binary search is used

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace smoothie::resource {

    // ── MPHF constants ──────────────────────────────────────────────────

    inline constexpr uint32_t mphf_magic = 0x4D504846; // "MPHF"
    inline constexpr uint32_t mphf_empty_seed = UINT32_MAX;

    // ── MPHF table header (stored in .lpak) ─────────────────────────────

#pragma pack(push, 1)
    struct mphf_header {
        uint32_t magic;        // mphf_magic
        uint32_t bucket_count; // number of buckets (== entry_count for load factor 1.0)
        uint32_t entry_count;  // number of entries
        uint32_t reserved;     // padding
        // Followed by bucket_count x uint32_t seeds
    };
#pragma pack(pop)
    static_assert(sizeof(mphf_header) == 16, "mphf_header must be 16 bytes");

    // ── MPHF probe function ─────────────────────────────────────────────

    /// Seeded hash: mix a 64-bit key with a 32-bit seed to produce an index.
    [[nodiscard]] constexpr auto mphf_probe(uint64_t key, uint32_t seed, uint32_t n) noexcept -> uint32_t {
        uint64_t h = key ^ (static_cast<uint64_t>(seed) * 2654435761ULL);
        h ^= h >> 33;
        h *= 0xFF51AFD7ED558CCDULL;
        h ^= h >> 33;
        return static_cast<uint32_t>(h % n);
    }

    /// Primary bucket assignment: hash -> bucket index.
    [[nodiscard]] constexpr auto mphf_bucket(uint64_t key, uint32_t bucket_count) noexcept -> uint32_t {
        return static_cast<uint32_t>(key % bucket_count);
    }

    // ── MPHF lookup (O(1), used at runtime) ─────────────────────────────

    /// Look up a key in the MPHF table. Returns the index into the entry array.
    [[nodiscard]] inline auto mphf_lookup(uint64_t key, std::span<const uint32_t> seeds, uint32_t entry_count) noexcept
        -> uint32_t {

        if (seeds.empty() || entry_count == 0) {
            return UINT32_MAX;
        }

        const auto bucket_count = static_cast<uint32_t>(seeds.size());
        const auto bucket = mphf_bucket(key, bucket_count);
        const auto seed = seeds[bucket];

        if (seed == mphf_empty_seed) {
            return UINT32_MAX;
        }

        return mphf_probe(key, seed, entry_count);
    }

    // ── MPHF builder (used at build time by pak_writer) ─────────────────

    /// Build an MPHF table from a set of 64-bit keys.
    [[nodiscard]] inline auto mphf_build(std::span<const uint64_t> keys) -> std::vector<uint32_t> {
        const auto n = static_cast<uint32_t>(keys.size());
        if (n == 0) {
            return {};
        }

        const uint32_t bucket_count = n;

        struct bucket_info {
            std::vector<uint32_t> key_indices;
        };
        std::vector<bucket_info> buckets(bucket_count);
        for (uint32_t i = 0; i < n; ++i) {
            auto b = mphf_bucket(keys[i], bucket_count);
            buckets[b].key_indices.push_back(i);
        }

        std::vector<uint32_t> bucket_order(bucket_count);
        for (uint32_t i = 0; i < bucket_count; ++i) {
            bucket_order[i] = i;
        }
        std::sort(bucket_order.begin(), bucket_order.end(), [&](uint32_t a, uint32_t b) {
            return buckets[a].key_indices.size() > buckets[b].key_indices.size();
        });

        std::vector<uint32_t> seeds(bucket_count, mphf_empty_seed);
        std::vector<bool> occupied(n, false);

        // Pre-allocate scratch buffers outside the loop to avoid per-bucket heap allocation.
        std::vector<uint32_t> slots;
        std::vector<bool> trial(n, false);

        for (auto bi : bucket_order) {
            const auto &bkt = buckets[bi];
            if (bkt.key_indices.empty()) {
                continue;
            }

            if (bkt.key_indices.size() == 1) {
                bool found = false;
                for (uint32_t seed = 0; seed < n * 8 + 256; ++seed) {
                    auto slot = mphf_probe(keys[bkt.key_indices[0]], seed, n);
                    if (!occupied[slot]) {
                        seeds[bi] = seed;
                        occupied[slot] = true;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return {};
                }
                continue;
            }

            bool found = false;
            for (uint32_t seed = 0; seed < n * 8; ++seed) {
                slots.clear();
                bool collision = false;

                for (auto ki : bkt.key_indices) {
                    auto slot = mphf_probe(keys[ki], seed, n);
                    if (occupied[slot] || trial[slot]) {
                        collision = true;
                        break;
                    }
                    trial[slot] = true;
                    slots.push_back(slot);
                }

                for (auto s : slots) {
                    trial[s] = false;
                }

                if (!collision) {
                    seeds[bi] = seed;
                    for (auto s : slots) {
                        occupied[s] = true;
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                return {};
            }
        }

        return seeds;
    }

    /// Serialize an MPHF table to bytes (header + seeds).
    [[nodiscard]] inline auto mphf_serialize(const std::vector<uint32_t> &seeds, uint32_t entry_count)
        -> std::vector<std::byte> {

        std::vector<std::byte> result;
        result.resize(sizeof(mphf_header) + seeds.size() * sizeof(uint32_t));

        auto *hdr = reinterpret_cast<mphf_header *>(result.data());
        hdr->magic = mphf_magic;
        hdr->bucket_count = static_cast<uint32_t>(seeds.size());
        hdr->entry_count = entry_count;
        hdr->reserved = 0;

        auto *dst = reinterpret_cast<uint32_t *>(result.data() + sizeof(mphf_header));
        for (size_t i = 0; i < seeds.size(); ++i) {
            dst[i] = seeds[i];
        }

        return result;
    }

    /// Deserialize MPHF seeds from a raw byte span (validates header).
    [[nodiscard]] inline auto mphf_deserialize(std::span<const std::byte> data) -> std::span<const uint32_t> {

        if (data.size() < sizeof(mphf_header)) {
            return {};
        }

        const auto *hdr = reinterpret_cast<const mphf_header *>(data.data());
        if (hdr->magic != mphf_magic) {
            return {};
        }

        const size_t expected = sizeof(mphf_header) + hdr->bucket_count * sizeof(uint32_t);
        if (data.size() < expected) {
            return {};
        }

        return std::span<const uint32_t>(reinterpret_cast<const uint32_t *>(data.data() + sizeof(mphf_header)),
                                         hdr->bucket_count);
    }

} // namespace smoothie::resource
