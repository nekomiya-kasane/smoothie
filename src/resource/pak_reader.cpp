#include "smoothie/resource/pak_reader.h"
#include "smoothie/resource/hash.h"
#include "smoothie/resource/mphf.h"

#include <algorithm>
#include <cstring>
#include <format>

namespace smoothie::resource {

auto pak_reader::open(std::span<const std::byte> buffer) -> diagnostic_result<pak_reader> {
    if (buffer.size() < sizeof(file_header)) {
        return std::unexpected(error{error_code::corrupted,
            std::format("buffer too small: {} bytes, need at least {}", buffer.size(), sizeof(file_header))});
    }

    pak_reader reader;
    reader.buffer_ = buffer;
    reader.header_ = reinterpret_cast<const file_header*>(buffer.data());

    // Validate magic
    if (std::memcmp(reader.header_->magic, lpak_magic.data(), 4) != 0) {
        return std::unexpected(error{error_code::corrupted, "invalid magic number"});
    }

    // Validate version
    const auto file_major = lpak_version_major_of(reader.header_->version);
    const auto file_minor = lpak_version_minor_of(reader.header_->version);
    if (file_major != lpak_version_major) {
        return std::unexpected(error{error_code::version_mismatch,
            std::format("incompatible major version: file={}, expected={}", file_major, lpak_version_major)});
    }
    if (file_minor > lpak_version_minor) {
        return std::unexpected(error{error_code::version_mismatch,
            std::format("file minor version {} > current {}", file_minor, lpak_version_minor)});
    }

    // Validate header checksum
    auto hdr_bytes = std::span<const std::byte>(buffer.data(), 0x38);
    const uint64_t expected_checksum = hash64(hdr_bytes);
    if (expected_checksum != reader.header_->header_checksum) {
        return std::unexpected(error{error_code::corrupted,
            std::format("header checksum mismatch: expected={:#018x}, got={:#018x}",
                expected_checksum, reader.header_->header_checksum)});
    }

    // Validate index bounds
    const auto index_offset = reader.header_->index_offset;
    const auto index_size = static_cast<size_t>(reader.header_->entry_count) * sizeof(entry_descriptor);
    if (index_size / sizeof(entry_descriptor) != reader.header_->entry_count ||
        index_offset > buffer.size() || index_size > buffer.size() - index_offset) {
        return std::unexpected(error{error_code::corrupted, "index extends beyond buffer"});
    }

    reader.index_ = std::span<const entry_descriptor>(
        reinterpret_cast<const entry_descriptor*>(buffer.data() + index_offset),
        reader.header_->entry_count);

    // Parse MPHF table if present
#if defined(SMOOTHIE_USE_MPHF)
    if (reader.header_->mphf_offset > 0 && reader.header_->mphf_size > 0) {
        const auto mphf_off = reader.header_->mphf_offset;
        const auto mphf_sz = reader.header_->mphf_size;
        if (mphf_off + mphf_sz <= buffer.size()) {
            auto mphf_span = buffer.subspan(mphf_off, mphf_sz);
            reader.mphf_seeds_ = mphf_deserialize(mphf_span);
        }
    }
#endif

    // Validate data payload bounds
    const auto data_off = reader.header_->data_offset;
    if (data_off > buffer.size()) {
        return std::unexpected(error{error_code::corrupted, "data_offset beyond buffer"});
    }

    const auto data_end = (reader.header_->string_table_offset > 0)
        ? reader.header_->string_table_offset
        : buffer.size();
    reader.data_payload_ = buffer.subspan(data_off, data_end - data_off);

    // O5+O6: Build Eytzinger layout on compact 8B hash keys.
    // Eytzinger BFS layout enables branchless binary search with hardware prefetch.
    // 8B stride → 8 keys per cache line (vs 2.67 for 24B entry_descriptor).
    const auto n = reader.index_.size();
    std::vector<uint64_t> sorted_hashes(n);
    for (size_t i = 0; i < n; ++i) {
        sorted_hashes[i] = reader.index_[i].semantic_hash;
    }
    reader.compact_hashes_.build(sorted_hashes);

    return reader;
}

auto pak_reader::entry_count() const noexcept -> uint32_t {
    return header_ ? header_->entry_count : 0;
}

auto pak_reader::header() const noexcept -> const file_header* {
    return header_;
}

auto pak_reader::entries() const noexcept -> std::span<const entry_descriptor> {
    return index_;
}

auto pak_reader::find(uint64_t semantic_hash) const noexcept -> const entry_descriptor* {
#if defined(SMOOTHIE_USE_MPHF)
    if (!mphf_seeds_.empty()) [[likely]] {
        auto idx = mphf_lookup(semantic_hash, mphf_seeds_, header_->entry_count);
        if (idx < index_.size() && index_[idx].semantic_hash == semantic_hash) [[likely]] {
            return &index_[idx];
        }
        return nullptr;
    }
#endif
    // O5+O6: Branchless Eytzinger search on compact 8B hash keys.
    auto idx = compact_hashes_.find(semantic_hash);
    if (idx < compact_hashes_.size()) [[likely]] {
        return &index_[idx];
    }
    return nullptr;
}

auto pak_reader::data_of(const entry_descriptor& entry) const noexcept
    -> result<std::span<const std::byte>> {
    const size_t end = static_cast<size_t>(entry.data_offset) + entry.data_size;
    if (end > data_payload_.size()) {
        return std::unexpected(error_code::corrupted);
    }
    return data_payload_.subspan(entry.data_offset, entry.data_size);
}

auto pak_reader::data_of_view(const entry_descriptor& entry) const
    -> diagnostic_result<resource_view> {
    const size_t end = static_cast<size_t>(entry.data_offset) + entry.data_size;
    if (end > data_payload_.size()) {
        return std::unexpected(error{error_code::corrupted, "entry data extends beyond payload"});
    }

    auto raw = data_payload_.subspan(entry.data_offset, entry.data_size);
    auto ef = static_cast<entry_flags>(entry.flags);

    if (!is_compressed(ef)) {
        return resource_view(raw);
    }

    auto decompressed = decompress(raw, entry.reserved, ef);
    if (!decompressed.has_value()) {
        return std::unexpected(decompressed.error());
    }

    return resource_view(std::move(*decompressed));
}

auto pak_reader::validate_index_checksum() const noexcept -> bool {
    if (!header_ || index_.empty()) return true;
    auto index_bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(index_.data()),
        index_.size() * sizeof(entry_descriptor));
    return hash64(index_bytes) == header_->index_checksum;
}

}  // namespace smoothie::resource
