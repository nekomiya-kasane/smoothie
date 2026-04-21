#pragma once

/// @file vfs.h
/// @brief Virtual File System for mounting .lpak packages and resource lookup.

#include "smoothie/exports.h"
#include "smoothie/resource/lpak_format.h"
#include "smoothie/resource/pak_reader.h"
#include "smoothie/stats.h"
#include "smoothie/types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace smoothie::resource {

    /// Information about a single mount point.
    struct mount_point_info {
        std::string name;
        int priority = 0;
        size_t resource_count = 0;
        size_t data_size = 0;
        bool is_embedded = false;
        bool is_mmap = false;
    };

    /// Virtual File System — manages mounted .lpak packages and provides resource lookup.
    ///
    /// Mount .lpak files with names and priorities. Higher priority mounts override lower ones
    /// when resources have the same semantic hash. After mount/unmount, the unified index is
    /// automatically rebuilt.
    class SMOOTHIE_API vfs {
      public:
        vfs();
        ~vfs();

        vfs(vfs &&) noexcept;
        vfs &operator=(vfs &&) noexcept;
        vfs(const vfs &) = delete;
        vfs &operator=(const vfs &) = delete;

        /// Mount a .lpak file. The file is read into memory and parsed.
        [[nodiscard]] auto mount(std::string_view name, const std::filesystem::path &path, int priority = 0)
            -> diagnostic_result<void>;

        /// Mount from an in-memory buffer (takes ownership).
        [[nodiscard]] auto mount(std::string_view name, std::vector<std::byte> data, int priority = 0)
            -> diagnostic_result<void>;

        /// Mount from a non-owning span (zero-copy).
        [[nodiscard]] auto mount_embedded(std::string_view name, std::span<const std::byte> data, int priority = 0)
            -> diagnostic_result<void>;

        /// Mount via memory-mapped file (zero-copy).
        [[nodiscard]] auto mount_mmap(std::string_view name, const std::filesystem::path &path, int priority = 0)
            -> diagnostic_result<void>;

        /// Unmount a previously mounted package by name.
        [[nodiscard]] auto unmount(std::string_view name) -> diagnostic_result<void>;

        /// Rebuild the unified index. Called automatically after mount/unmount.
        void rebuild_index();

        /// O(log N) lookup by semantic hash.
        [[nodiscard]] auto get(uint64_t semantic_hash) const -> result<std::span<const std::byte>>;

        /// Strong-typed lookup (compile-time ID).
        template <typename TAssetId> [[nodiscard]] auto get() const -> result<std::span<const std::byte>> {
            return get(TAssetId::id);
        }

        /// Dynamic URI lookup.
        [[nodiscard]] auto get_dynamic(std::string_view uri) const -> result<resource_view>;

        /// Locale-aware lookup.
        [[nodiscard]] auto get_localized(uint64_t base_hash, std::string_view uri, std::string_view current_locale,
                                         std::span<const std::string> fallback_chain) const
            -> result<std::span<const std::byte>>;

        /// Get resource data with transparent decompression.
        [[nodiscard]] auto get_view(uint64_t semantic_hash) const -> diagnostic_result<resource_view>;

        /// Get entry metadata without fetching data.
        [[nodiscard]] auto get_entry_info(uint64_t semantic_hash) const -> result<entry_descriptor>;

        /// Check if a resource exists.
        [[nodiscard]] auto contains(uint64_t semantic_hash) const noexcept -> bool;

        /// Number of currently mounted packages.
        [[nodiscard]] auto mount_count() const noexcept -> size_t;

        /// Total number of resources in the unified index.
        [[nodiscard]] auto resource_count() const noexcept -> size_t;

        /// List the names of all currently mounted packages.
        [[nodiscard]] auto mount_names() const -> std::vector<std::string>;

        /// Return all resource semantic hashes in the unified index.
        [[nodiscard]] auto enumerate() const -> std::vector<uint64_t>;

        /// Return detailed information about each mount point.
        [[nodiscard]] auto mount_info() const -> std::vector<mount_point_info>;

        /// Locale-aware lookup with transparent decompression.
        [[nodiscard]] auto get_view_localized(uint64_t base_hash, std::string_view uri, std::string_view current_locale,
                                              std::span<const std::string> fallback_chain) const
            -> diagnostic_result<resource_view>;

        /// Register a callback invoked when a mounted .lpak file changes on disk.
        using watch_callback = std::function<void(std::string_view mount_name)>;
        [[nodiscard]] auto watch(watch_callback cb) -> uint64_t;

        /// Unregister a previously registered watch callback by token.
        void unwatch(uint64_t token);

        // ── Performance counters ────────────────────────────────────────

        /// Return a snapshot of the VFS performance counters.
        [[nodiscard]] auto stats() const noexcept -> vfs_stats;

        /// Reset all performance counters to zero.
        void reset_stats() noexcept;

      private:
        struct vfs_snapshot;
        struct impl;
#if defined(_MSC_VER)
#    pragma warning(push)
#    pragma warning(disable : 4251)
#endif
        std::unique_ptr<impl> impl_;
#if defined(_MSC_VER)
#    pragma warning(pop)
#endif
    };

} // namespace smoothie::resource
