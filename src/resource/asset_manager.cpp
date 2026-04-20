#include "smoothie/resource/asset_manager.h"

#include <thread>

namespace smoothie::resource {

asset_manager::asset_manager(vfs& filesystem, size_t max_cache_size)
    : vfs_(filesystem), max_cache_size_(max_cache_size) {}

void asset_manager::register_loader(uint32_t type_id, asset_loader loader) {
    loaders_[type_id] = std::move(loader);
}

void asset_manager::register_loader(std::string_view type_name, asset_loader loader) {
    register_loader(hash32(type_name), std::move(loader));
}

auto asset_manager::has_loader(uint32_t type_id) const noexcept -> bool {
    return loaders_.contains(type_id);
}

auto asset_manager::load(uint64_t semantic_hash, uint32_t type_id)
    -> result<loaded_asset> {
    // Check cache first
    if (max_cache_size_ > 0) {
        std::lock_guard lock(cache_mutex_);
        auto cit = cache_.find(semantic_hash);
        if (cit != cache_.end()) {
            touch_cache(semantic_hash);
            return cit->second.asset;
        }
    }

    // Load dependencies first
    auto dit = deps_.find(semantic_hash);
    if (dit != deps_.end()) {
        for (const auto& [dep_hash, dep_type] : dit->second) {
            auto dep_result = load(dep_hash, dep_type);
            if (!dep_result.has_value()) {
                return std::unexpected(dep_result.error());
            }
        }
    }

    auto it = loaders_.find(type_id);
    if (it == loaders_.end()) {
        return std::unexpected(error_code::not_found);
    }

    auto raw = vfs_.get(semantic_hash);
    if (!raw.has_value()) {
        return std::unexpected(raw.error());
    }

    auto loaded = it->second(*raw);
    if (!loaded.has_value()) {
        return loaded;
    }

    // Invoke on_loaded callback
    if (on_loaded_) {
        on_loaded_(semantic_hash, *loaded);
    }

    // Insert into cache
    if (max_cache_size_ > 0) {
        std::lock_guard lock(cache_mutex_);
        while (cache_.size() >= max_cache_size_) {
            evict_lru();
        }
        lru_order_.push_front(semantic_hash);
        cache_[semantic_hash] = cache_entry{*loaded, lru_order_.begin()};
    }

    return loaded;
}

auto asset_manager::load(uint64_t semantic_hash, std::string_view type_name)
    -> result<loaded_asset> {
    return load(semantic_hash, hash32(type_name));
}

auto asset_manager::load_async(uint64_t semantic_hash, uint32_t type_id)
    -> std::future<result<loaded_asset>> {
    return std::async(std::launch::async, [this, semantic_hash, type_id]() {
        return load(semantic_hash, type_id);
    });
}

void asset_manager::unload(uint64_t semantic_hash) {
    std::lock_guard lock(cache_mutex_);
    auto it = cache_.find(semantic_hash);
    if (it != cache_.end()) {
        lru_order_.erase(it->second.lru_it);
        cache_.erase(it);
    }
}

void asset_manager::clear_cache() {
    std::lock_guard lock(cache_mutex_);
    cache_.clear();
    lru_order_.clear();
}

auto asset_manager::cache_size() const noexcept -> size_t {
    std::lock_guard lock(cache_mutex_);
    return cache_.size();
}

auto asset_manager::max_cache_size() const noexcept -> size_t {
    return max_cache_size_;
}

void asset_manager::add_dependency(uint64_t dependent, uint64_t dependency, uint32_t dep_type_id) {
    deps_[dependent].emplace_back(dependency, dep_type_id);
}

auto asset_manager::dependencies(uint64_t semantic_hash) const
    -> std::vector<std::pair<uint64_t, uint32_t>> {
    auto it = deps_.find(semantic_hash);
    if (it != deps_.end()) {
        return it->second;
    }
    return {};
}

void asset_manager::set_on_loaded(on_loaded_callback cb) {
    on_loaded_ = std::move(cb);
}

auto asset_manager::loader_count() const noexcept -> size_t {
    return loaders_.size();
}

void asset_manager::evict_lru() {
    if (lru_order_.empty()) return;
    auto oldest = lru_order_.back();
    lru_order_.pop_back();
    cache_.erase(oldest);
}

void asset_manager::touch_cache(uint64_t semantic_hash) {
    auto it = cache_.find(semantic_hash);
    if (it != cache_.end()) {
        lru_order_.erase(it->second.lru_it);
        lru_order_.push_front(semantic_hash);
        it->second.lru_it = lru_order_.begin();
    }
}

}  // namespace smoothie::resource
