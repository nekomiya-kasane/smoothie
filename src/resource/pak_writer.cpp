#include "smoothie/resource/pak_writer.h"
#include "smoothie/resource/hash.h"
#include "smoothie/resource/mphf.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <format>
#include <future>

namespace smoothie::resource {

void pak_writer::add(std::string_view path, resource_type type, std::span<const std::byte> data) {
    entries_.push_back({
        .path = std::string(path),
        .type = type,
        .data = std::vector<std::byte>(data.begin(), data.end()),
        .compress = default_compress_,
    });
}

void pak_writer::add(std::string_view path, resource_type type, std::span<const std::byte> data,
                     compression_mode compress) {
    entries_.push_back({
        .path = std::string(path),
        .type = type,
        .data = std::vector<std::byte>(data.begin(), data.end()),
        .compress = compress,
    });
}

void pak_writer::set_default_compression(compression_mode mode) noexcept {
    default_compress_ = mode;
}

auto pak_writer::entry_count() const noexcept -> size_t {
    return entries_.size();
}

auto pak_writer::write(const std::filesystem::path& output) const -> diagnostic_result<void> {
    if (entries_.empty()) {
        return std::unexpected(error{error_code::invalid_argument, "no entries to write"});
    }

    // Phase 1: compress all entries in parallel.
    struct compressed_entry {
        std::vector<std::byte> data;        // compressed (or original if compression didn't help)
        entry_flags flags = entry_flags::none;
        uint32_t uncompressed_size = 0;
    };

    const auto n_entries = entries_.size();
    std::vector<std::future<compressed_entry>> futures;
    futures.reserve(n_entries);

    for (const auto& e : entries_) {
        futures.push_back(std::async(std::launch::async, [&e]() -> compressed_entry {
            if (e.compress == compression_mode::none) {
                return {.data = e.data, .flags = entry_flags::none, .uncompressed_size = 0};
            }
            auto comp_result = compress(e.data, e.compress);
            if (comp_result.has_value() && comp_result->size() < e.data.size()) {
                return {
                    .data = std::move(*comp_result),
                    .flags = compression_to_flags(e.compress),
                    .uncompressed_size = static_cast<uint32_t>(e.data.size()),
                };
            }
            return {.data = e.data, .flags = entry_flags::none, .uncompressed_size = 0};
        }));
    }

    // Phase 2: collect results and assemble data payload sequentially.
    std::vector<compressed_entry> compressed(n_entries);
    for (size_t i = 0; i < n_entries; ++i) {
        compressed[i] = futures[i].get();
    }

    std::vector<entry_descriptor> index;
    index.reserve(n_entries);

    std::vector<std::byte> data_payload;
    for (size_t i = 0; i < n_entries; ++i) {
        const auto& e = entries_[i];
        const auto& ce = compressed[i];
        const auto offset = static_cast<uint32_t>(data_payload.size());
        const auto actual_size = static_cast<uint32_t>(ce.data.size());

        index.push_back({
            .semantic_hash = hash64(e.path),
            .data_offset   = offset,
            .data_size     = actual_size,
            .type          = static_cast<uint16_t>(e.type),
            .flags         = static_cast<uint16_t>(ce.flags),
            .reserved      = ce.uncompressed_size,
        });

        data_payload.insert(data_payload.end(), ce.data.begin(), ce.data.end());

        const auto aligned = align_up(data_payload.size(), data_alignment);
        data_payload.resize(aligned, std::byte{0});
    }

    std::sort(index.begin(), index.end(),
        [](const entry_descriptor& a, const entry_descriptor& b) {
            return a.semantic_hash < b.semantic_hash;
        });

    std::vector<std::byte> string_table;
    for (const auto& e : entries_) {
        const auto* p = reinterpret_cast<const std::byte*>(e.path.data());
        string_table.insert(string_table.end(), p, p + e.path.size());
        string_table.push_back(std::byte{0});
    }

    std::vector<uint64_t> sorted_keys;
    sorted_keys.reserve(index.size());
    for (const auto& ed : index) sorted_keys.push_back(ed.semantic_hash);

    auto mphf_seeds = mphf_build(sorted_keys);
    std::vector<std::byte> mphf_blob;
    if (!mphf_seeds.empty()) {
        mphf_blob = mphf_serialize(mphf_seeds, static_cast<uint32_t>(index.size()));

        const auto n = static_cast<uint32_t>(index.size());
        std::vector<entry_descriptor> reordered(n);
        for (const auto& ed : index) {
            auto slot = mphf_lookup(ed.semantic_hash, mphf_seeds, n);
            reordered[slot] = ed;
        }
        index = std::move(reordered);
    }

    constexpr size_t header_size = sizeof(file_header);
    const size_t mphf_offset_val = mphf_blob.empty() ? 0 : header_size;
    const size_t mphf_size_val = mphf_blob.size();
    const size_t index_offset = header_size + mphf_size_val;
    const size_t index_size = index.size() * sizeof(entry_descriptor);
    const size_t data_offset_val = align_up(index_offset + index_size, data_alignment);
    const size_t string_table_offset = data_offset_val + data_payload.size();

    auto index_bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(index.data()), index_size);
    auto data_bytes = std::span<const std::byte>(data_payload);

    const uint64_t index_checksum = hash64(index_bytes);
    const uint64_t data_checksum = hash64(data_bytes);

    file_header hdr{};
    std::memcpy(hdr.magic, lpak_magic.data(), 4);
    hdr.version = lpak_version;
    hdr.flags = 0;
    hdr.entry_count = static_cast<uint32_t>(index.size());
    hdr.mphf_offset = mphf_offset_val;
    hdr.mphf_size = mphf_size_val;
    hdr.index_offset = index_offset;
    hdr.data_offset = data_offset_val;
    hdr.string_table_offset = string_table_offset;
    hdr.index_checksum = index_checksum;
    hdr.data_checksum = data_checksum;

    hdr.header_checksum = 0;
    auto hdr_bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(&hdr), 0x38);
    hdr.header_checksum = hash64(hdr_bytes);

    auto temp_path = output;
    temp_path += ".tmp";

    {
        std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return std::unexpected(error{error_code::io_error,
                std::format("failed to open '{}' for writing", temp_path.string())});
        }

        out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

        if (!mphf_blob.empty()) {
            out.write(reinterpret_cast<const char*>(mphf_blob.data()),
                static_cast<std::streamsize>(mphf_blob.size()));
        }

        out.write(reinterpret_cast<const char*>(index.data()),
            static_cast<std::streamsize>(index_size));

        const size_t pad_size = data_offset_val - (index_offset + index_size);
        if (pad_size > 0) {
            std::vector<char> pad(pad_size, '\0');
            out.write(pad.data(), static_cast<std::streamsize>(pad_size));
        }

        out.write(reinterpret_cast<const char*>(data_payload.data()),
            static_cast<std::streamsize>(data_payload.size()));

        out.write(reinterpret_cast<const char*>(string_table.data()),
            static_cast<std::streamsize>(string_table.size()));

        if (!out) {
            return std::unexpected(error{error_code::io_error,
                std::format("write failed for '{}'", temp_path.string())});
        }
    }

    std::error_code ec;
    std::filesystem::rename(temp_path, output, ec);
    if (ec) {
        std::filesystem::remove(temp_path, ec);
        return std::unexpected(error{error_code::io_error,
            std::format("rename '{}' -> '{}' failed: {}", temp_path.string(), output.string(), ec.message())});
    }

    return {};
}

}  // namespace smoothie::resource
