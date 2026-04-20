#pragma once

/// @file pak_writer.h
/// @brief .lpak file writer: packs resources into the binary format with optional MPHF and compression.

#include "smoothie/exports.h"
#include "smoothie/resource/compression.h"
#include "smoothie/resource/lpak_format.h"
#include "smoothie/types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace smoothie::resource {

/// Builder for .lpak files.
///
/// Usage:
///   pak_writer w;
///   w.add("core/images/icon.png", resource_type::image, icon_bytes);
///   w.add("core/fonts/roboto.ttf", resource_type::font, font_bytes);
///   auto r = w.write("output.lpak");
class SMOOTHIE_API pak_writer {
  public:
    pak_writer() = default;

    /// Add a resource entry. The data is copied into the internal buffer.
    void add(std::string_view path, resource_type type, std::span<const std::byte> data);

    /// Add a resource entry with per-entry compression mode.
    void add(std::string_view path, resource_type type, std::span<const std::byte> data, compression_mode compress);

    /// Set the default compression mode for entries added without an explicit compression_mode.
    void set_default_compression(compression_mode mode) noexcept;

    /// Write the .lpak file to disk.
    [[nodiscard]] auto write(const std::filesystem::path &output) const -> diagnostic_result<void>;

    /// Return the number of entries added so far.
    [[nodiscard]] auto entry_count() const noexcept -> size_t;

  private:
    struct pending_entry {
        std::string path;
        resource_type type;
        std::vector<std::byte> data;
        compression_mode compress = compression_mode::none;
    };
    compression_mode default_compress_ = compression_mode::none;
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4251)
#endif
    std::vector<pending_entry> entries_;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
};

} // namespace smoothie::resource
