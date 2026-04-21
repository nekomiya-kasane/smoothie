#pragma once

/// @file asset_manager.h
/// @brief High-level asset manager wrapping VFS with strong-typed access and locale awareness.

#include "smoothie/exports.h"
#include "smoothie/resource/hash.h"
#include "smoothie/resource/vfs.h"
#include "smoothie/types.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <list>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace smoothie::resource {

    /// Type-erased loaded asset handle.
    struct loaded_asset {
        uint32_t type_id = 0;
        std::vector<std::byte> data;
    };

    /// Custom loader function signature.
    using asset_loader = std::function<result<loaded_asset>(std::span<const std::byte>)>;

    /// Callback invoked when an asset finishes loading.
    using on_loaded_callback = std::function<void(uint64_t semantic_hash, const loaded_asset &asset)>;

    /// Asset manager — typed resource loading layer on top of VFS.
    ///
    /// Register loaders for custom resource types (textures, fonts, layouts, etc.).
    /// Includes an LRU cache, async loading, dependency tracking, and load callbacks.
    class SMOOTHIE_API asset_manager {
      public:
        /// Construct with a VFS reference and optional LRU cache capacity.
        explicit asset_manager(vfs &filesystem, size_t max_cache_size = 128);

        /// Register a loader for a given type ID.
        void register_loader(uint32_t type_id, asset_loader loader);

        /// Register a loader for a named type (hashed to type_id via hash32).
        void register_loader(std::string_view type_name, asset_loader loader);

        /// Check if a loader is registered for the given type ID.
        [[nodiscard]] auto has_loader(uint32_t type_id) const noexcept -> bool;

        /// Load a resource by semantic hash and type ID.
        [[nodiscard]] auto load(uint64_t semantic_hash, uint32_t type_id) -> result<loaded_asset>;

        /// Load a resource by semantic hash and type name.
        [[nodiscard]] auto load(uint64_t semantic_hash, std::string_view type_name) -> result<loaded_asset>;

        /// Asynchronously load a resource.
        [[nodiscard]] auto load_async(uint64_t semantic_hash, uint32_t type_id) -> std::future<result<loaded_asset>>;

        /// Evict a specific resource from the cache.
        void unload(uint64_t semantic_hash);

        /// Clear the entire LRU cache.
        void clear_cache();

        /// Number of items currently in the cache.
        [[nodiscard]] auto cache_size() const noexcept -> size_t;

        /// Maximum cache capacity.
        [[nodiscard]] auto max_cache_size() const noexcept -> size_t;

        /// Register a dependency: loading `dependent` will first load `dependency`.
        void add_dependency(uint64_t dependent, uint64_t dependency, uint32_t dep_type_id);

        /// Query dependencies for a given resource.
        [[nodiscard]] auto dependencies(uint64_t semantic_hash) const -> std::vector<std::pair<uint64_t, uint32_t>>;

        /// Register a callback invoked after each successful load.
        void set_on_loaded(on_loaded_callback cb);

        /// Number of registered loaders.
        [[nodiscard]] auto loader_count() const noexcept -> size_t;

      private:
        void evict_lru();
        void touch_cache(uint64_t semantic_hash);

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4251)
#endif
        vfs &vfs_;
        std::map<uint32_t, asset_loader> loaders_;

        size_t max_cache_size_;
        std::list<uint64_t> lru_order_;
        struct cache_entry {
            loaded_asset asset;
            std::list<uint64_t>::iterator lru_it;
        };
        std::unordered_map<uint64_t, cache_entry> cache_;
        mutable std::mutex cache_mutex_;

        std::map<uint64_t, std::vector<std::pair<uint64_t, uint32_t>>> deps_;

        on_loaded_callback on_loaded_;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    };

} // namespace smoothie::resource
