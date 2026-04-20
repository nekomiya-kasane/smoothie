#pragma once

/// @file lpak_format.h
/// @brief On-disk .lpak binary format: file_header, entry_descriptor, and utilities.

#include <array>
#include <cstddef>
#include <cstdint>

namespace smoothie::resource {

// ── .lpak magic number ──────────────────────────────────────────────────

inline constexpr std::array<char, 4> lpak_magic = {'L', 'P', 'A', 'K'};

// ── Version encoding: major << 16 | minor ───────────────────────────────

inline constexpr uint16_t lpak_version_major = 1;
inline constexpr uint16_t lpak_version_minor = 0;
inline constexpr uint32_t lpak_version =
    (static_cast<uint32_t>(lpak_version_major) << 16) | lpak_version_minor;

[[nodiscard]] constexpr auto make_lpak_version(uint16_t major, uint16_t minor) noexcept -> uint32_t {
    return (static_cast<uint32_t>(major) << 16) | minor;
}

[[nodiscard]] constexpr auto lpak_version_major_of(uint32_t v) noexcept -> uint16_t {
    return static_cast<uint16_t>(v >> 16);
}

[[nodiscard]] constexpr auto lpak_version_minor_of(uint32_t v) noexcept -> uint16_t {
    return static_cast<uint16_t>(v & 0xFFFF);
}

// ── File header (80 bytes = 0x50) ───────────────────────────────────────

#pragma pack(push, 1)
struct file_header {
    char     magic[4];              // 0x0000  "LPAK"
    uint32_t version;               // 0x0004  major << 16 | minor
    uint32_t flags;                 // 0x0008  compression, endianness
    uint32_t entry_count;           // 0x000C
    uint64_t mphf_offset;           // 0x0010
    uint64_t mphf_size;             // 0x0018
    uint64_t index_offset;          // 0x0020
    uint64_t data_offset;           // 0x0028
    uint64_t string_table_offset;   // 0x0030
    uint64_t header_checksum;       // 0x0038  xxHash64 of bytes 0x0000–0x0037
    uint64_t index_checksum;        // 0x0040  xxHash64 of index array
    uint64_t data_checksum;         // 0x0048  xxHash64 of data payload (optional)
};
#pragma pack(pop)
static_assert(sizeof(file_header) == 80, "file_header must be exactly 80 bytes");

// ── Entry flags ─────────────────────────────────────────────────────────

enum class entry_flags : uint16_t {
    none       = 0x0000,
    compressed = 0x0001,
    lz4        = 0x0002,
    zstd       = 0x0004,
    encrypted  = 0x0008,
    streamed   = 0x0010,
};

[[nodiscard]] constexpr auto operator|(entry_flags a, entry_flags b) noexcept -> entry_flags {
    return static_cast<entry_flags>(
        static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

[[nodiscard]] constexpr auto operator&(entry_flags a, entry_flags b) noexcept -> entry_flags {
    return static_cast<entry_flags>(
        static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
}

[[nodiscard]] constexpr auto has_flag(entry_flags value, entry_flags flag) noexcept -> bool {
    return (value & flag) == flag;
}

// ── Entry descriptor (24 bytes, POD) ────────────────────────────────────

#pragma pack(push, 1)
struct entry_descriptor {
    uint64_t semantic_hash;   // 64-bit hash of the resource path
    uint32_t data_offset;     // offset relative to file_header::data_offset
    uint32_t data_size;       // size in bytes
    uint16_t type;            // resource type enum
    uint16_t flags;           // entry_flags bitmask
    uint32_t reserved;        // alignment / future (uncompressed_size when compressed)
};
#pragma pack(pop)
static_assert(sizeof(entry_descriptor) == 24, "entry_descriptor must be exactly 24 bytes");

// ── Resource type enum ──────────────────────────────────────────────────

enum class resource_type : uint16_t {
    unknown   = 0,
    image     = 1,
    font      = 2,
    i18n      = 3,
    bytecode  = 4,
    raw       = 5,
    shader    = 6,
    model     = 7,
    material  = 8,
    scene     = 9,
    ui_layout = 10,
    config    = 11,
    audio     = 12,
    data      = 13,
};

// ── 4KB alignment for data blocks ───────────────────────────────────────

inline constexpr size_t data_alignment = 4096;

[[nodiscard]] constexpr auto align_up(size_t value, size_t alignment) noexcept -> size_t {
    return (value + alignment - 1) & ~(alignment - 1);
}

}  // namespace smoothie::resource
