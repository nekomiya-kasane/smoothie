#include "smoothie/resource/vfs.h"

#include "detail/snapshot_holder.h"
#include "smoothie/detail/eytzinger_array.h"
#include "smoothie/resource/compression.h"
#include "smoothie/resource/hash.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <mutex>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace smoothie::resource {

// ── mmap_file: RAII memory-mapped file ──────────────────────────────────

struct mmap_file {
    const std::byte *data = nullptr;
    size_t size = 0;

#if defined(_WIN32)
    void *file_handle = INVALID_HANDLE_VALUE;
    void *mapping_handle = nullptr;
#else
    int fd = -1;
#endif

    mmap_file() = default;
    mmap_file(const mmap_file &) = delete;
    mmap_file &operator=(const mmap_file &) = delete;
    mmap_file(mmap_file &&o) noexcept
        : data(o.data), size(o.size)
#if defined(_WIN32)
          ,
          file_handle(o.file_handle), mapping_handle(o.mapping_handle)
#else
          ,
          fd(o.fd)
#endif
    {
        o.data = nullptr;
        o.size = 0;
#if defined(_WIN32)
        o.file_handle = INVALID_HANDLE_VALUE;
        o.mapping_handle = nullptr;
#else
        o.fd = -1;
#endif
    }
    mmap_file &operator=(mmap_file &&o) noexcept {
        if (this != &o) {
            close();
            data = o.data;
            size = o.size;
#if defined(_WIN32)
            file_handle = o.file_handle;
            mapping_handle = o.mapping_handle;
            o.file_handle = INVALID_HANDLE_VALUE;
            o.mapping_handle = nullptr;
#else
            fd = o.fd;
            o.fd = -1;
#endif
            o.data = nullptr;
            o.size = 0;
        }
        return *this;
    }
    ~mmap_file() { close(); }

    [[nodiscard]] auto is_open() const noexcept -> bool { return data != nullptr; }
    [[nodiscard]] auto span() const noexcept -> std::span<const std::byte> { return {data, size}; }

    static auto open(const std::filesystem::path &path) -> std::expected<mmap_file, error> {
        mmap_file m;
#if defined(_WIN32)
        m.file_handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m.file_handle == INVALID_HANDLE_VALUE) {
            return std::unexpected(
                error{error_code::io_error, std::format("mmap: failed to open '{}'", path.string())});
        }
        LARGE_INTEGER file_size;
        if (!GetFileSizeEx(m.file_handle, &file_size)) {
            CloseHandle(m.file_handle);
            m.file_handle = INVALID_HANDLE_VALUE;
            return std::unexpected(error{error_code::io_error, "mmap: GetFileSizeEx failed"});
        }
        m.size = static_cast<size_t>(file_size.QuadPart);
        if (m.size == 0) {
            CloseHandle(m.file_handle);
            m.file_handle = INVALID_HANDLE_VALUE;
            return std::unexpected(error{error_code::io_error, "mmap: empty file"});
        }
        m.mapping_handle = CreateFileMappingW(m.file_handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!m.mapping_handle) {
            CloseHandle(m.file_handle);
            m.file_handle = INVALID_HANDLE_VALUE;
            return std::unexpected(error{error_code::mmap_failed, "mmap: CreateFileMapping failed"});
        }
        auto *ptr = MapViewOfFile(m.mapping_handle, FILE_MAP_READ, 0, 0, 0);
        if (!ptr) {
            CloseHandle(m.mapping_handle);
            m.mapping_handle = nullptr;
            CloseHandle(m.file_handle);
            m.file_handle = INVALID_HANDLE_VALUE;
            return std::unexpected(error{error_code::mmap_failed, "mmap: MapViewOfFile failed"});
        }
        m.data = static_cast<const std::byte *>(ptr);
#else
        m.fd = ::open(path.c_str(), O_RDONLY);
        if (m.fd < 0) {
            return std::unexpected(
                error{error_code::io_error, std::format("mmap: failed to open '{}'", path.string())});
        }
        struct stat st;
        if (fstat(m.fd, &st) != 0 || st.st_size == 0) {
            ::close(m.fd);
            m.fd = -1;
            return std::unexpected(error{error_code::io_error, "mmap: fstat failed or empty"});
        }
        m.size = static_cast<size_t>(st.st_size);
        auto *ptr = ::mmap(nullptr, m.size, PROT_READ, MAP_PRIVATE, m.fd, 0);
        if (ptr == MAP_FAILED) {
            ::close(m.fd);
            m.fd = -1;
            return std::unexpected(error{error_code::mmap_failed, "mmap: mmap failed"});
        }
        m.data = static_cast<const std::byte *>(ptr);
#endif
        return m;
    }

    void close() noexcept {
#if defined(_WIN32)
        if (data) {
            UnmapViewOfFile(data);
            data = nullptr;
        }
        if (mapping_handle) {
            CloseHandle(mapping_handle);
            mapping_handle = nullptr;
        }
        if (file_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(file_handle);
            file_handle = INVALID_HANDLE_VALUE;
        }
#else
        if (data) {
            ::munmap(const_cast<std::byte *>(data), size);
            data = nullptr;
        }
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
#endif
        size = 0;
    }
};

struct mount_point {
    std::string name;
    int priority = 0;
    std::vector<std::byte> owned_data;
    std::span<const std::byte> embedded_data;
    std::shared_ptr<mmap_file> mmap_data;
    bool is_embedded = false;
    bool is_mmap = false;
    pak_reader reader;
};

struct resolved_entry {
    const std::byte *data_base_ptr; // precomputed: mount_data(mp).data() + header->data_offset
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t buf_total_size; // mount_data(mp).size() for bounds check
    uint32_t reserved;
    uint16_t pack_idx;
    uint16_t flags;
    uint16_t type;
    uint16_t _pad;
};

static auto mount_data(const mount_point &mp) noexcept -> std::span<const std::byte> {
    if (mp.is_mmap) {
        return mp.mmap_data->span();
    }
    if (mp.is_embedded) {
        return mp.embedded_data;
    }
    return std::span<const std::byte>(mp.owned_data);
}

// ── vfs_snapshot ────────────────────────────────────────────────────────

struct vfs::vfs_snapshot {
    std::vector<std::shared_ptr<const mount_point>> mounts;
    std::vector<std::pair<std::string, uint16_t>> name_index;
    smoothie::detail::eytzinger_array<uint64_t> index_hashes;
    std::vector<resolved_entry> index_entries;

    void rebuild() {
        name_index.clear();
        for (uint16_t i = 0; i < static_cast<uint16_t>(mounts.size()); ++i) {
            name_index.emplace_back(mounts[i]->name, i);
        }
        std::sort(name_index.begin(), name_index.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

        struct candidate {
            uint64_t hash;
            int priority;
            resolved_entry entry;
        };
        std::vector<candidate> all;

        for (uint16_t i = 0; i < static_cast<uint16_t>(mounts.size()); ++i) {
            const auto &mp = *mounts[i];
            const auto buf = mount_data(mp);
            const auto data_off = mp.reader.header()->data_offset;
            const auto *base = buf.data() + data_off;
            const auto buf_sz = static_cast<uint32_t>(buf.size());
            for (const auto &ed : mp.reader.entries()) {
                all.push_back({
                    .hash = ed.semantic_hash,
                    .priority = mp.priority,
                    .entry =
                        {
                            .data_base_ptr = base,
                            .data_offset = ed.data_offset,
                            .data_size = ed.data_size,
                            .buf_total_size = buf_sz,
                            .reserved = ed.reserved,
                            .pack_idx = i,
                            .flags = ed.flags,
                            .type = ed.type,
                            ._pad = 0,
                        },
                });
            }
        }

        std::sort(all.begin(), all.end(), [](const candidate &a, const candidate &b) {
            if (a.hash != b.hash) {
                return a.hash < b.hash;
            }
            return a.priority > b.priority;
        });

        std::vector<uint64_t> sorted_hashes;
        index_entries.clear();
        for (const auto &c : all) {
            if (sorted_hashes.empty() || sorted_hashes.back() != c.hash) {
                sorted_hashes.push_back(c.hash);
                index_entries.push_back(c.entry);
            }
        }

        index_hashes.build(sorted_hashes);
    }
};

// ── vfs::impl ───────────────────────────────────────────────────────────

struct vfs::impl {
    smoothie::detail::snapshot_holder<vfs_snapshot> snapshot;
    std::mutex write_mutex;

#ifdef SMOOTHIE_ENABLE_STATS
    smoothie::atomic_vfs_counters counters;
#endif

    impl() { snapshot.store(std::make_shared<const vfs_snapshot>()); }
};

// ── vfs ─────────────────────────────────────────────────────────────────

vfs::vfs() : impl_(std::make_unique<impl>()) {}
vfs::~vfs() = default;
vfs::vfs(vfs &&) noexcept = default;
vfs &vfs::operator=(vfs &&) noexcept = default;

auto vfs::mount(std::string_view name, const std::filesystem::path &path, int priority) -> diagnostic_result<void> {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return std::unexpected(error{error_code::io_error, std::format("failed to open '{}'", path.string())});
    }
    auto size = in.tellg();
    in.seekg(0);
    std::vector<std::byte> file_data(static_cast<size_t>(size));
    in.read(reinterpret_cast<char *>(file_data.data()), size);
    if (!in) {
        return std::unexpected(error{error_code::io_error, std::format("failed to read '{}'", path.string())});
    }

    return mount(name, std::move(file_data), priority);
}

auto vfs::mount(std::string_view name, std::vector<std::byte> data, int priority) -> diagnostic_result<void> {
    std::lock_guard lock(impl_->write_mutex);

    auto old = impl_->snapshot.load();

    for (const auto &m : old->mounts) {
        if (m->name == name) {
            return std::unexpected(error{error_code::already_mounted, std::format("mount '{}' already exists", name)});
        }
    }

    auto mp = std::make_shared<mount_point>();
    mp->name = std::string(name);
    mp->priority = priority;
    mp->owned_data = std::move(data);
    mp->is_embedded = false;

    auto parsed = pak_reader::open(mp->owned_data);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    mp->reader = std::move(*parsed);

    auto next = std::make_shared<vfs_snapshot>(*old);
    next->mounts.push_back(std::move(mp));
    next->rebuild();

    SMOOTHIE_STAT_INC(impl_->counters.mount_count);
    SMOOTHIE_STAT_ADD(impl_->counters.total_bytes, next->mounts.back()->owned_data.size());
    impl_->snapshot.store(std::move(next));
    return {};
}

auto vfs::mount_embedded(std::string_view name, std::span<const std::byte> data, int priority)
    -> diagnostic_result<void> {
    std::lock_guard lock(impl_->write_mutex);

    auto old = impl_->snapshot.load();

    for (const auto &m : old->mounts) {
        if (m->name == name) {
            return std::unexpected(error{error_code::already_mounted, std::format("mount '{}' already exists", name)});
        }
    }

    auto reader_result = pak_reader::open(data);
    if (!reader_result) {
        return std::unexpected(reader_result.error());
    }

    auto mp = std::make_shared<mount_point>();
    mp->name = std::string(name);
    mp->priority = priority;
    mp->embedded_data = data;
    mp->is_embedded = true;
    mp->reader = std::move(*reader_result);

    auto next = std::make_shared<vfs_snapshot>(*old);
    next->mounts.push_back(std::move(mp));
    next->rebuild();

    SMOOTHIE_STAT_INC(impl_->counters.mount_count);
    SMOOTHIE_STAT_ADD(impl_->counters.total_bytes, data.size());
    impl_->snapshot.store(std::move(next));
    return {};
}

auto vfs::mount_mmap(std::string_view name, const std::filesystem::path &path, int priority)
    -> diagnostic_result<void> {
    auto mmap_result = mmap_file::open(path);
    if (!mmap_result) {
        return std::unexpected(mmap_result.error());
    }

    auto mmap_ptr = std::make_shared<mmap_file>(std::move(*mmap_result));
    const auto mmap_total_size = mmap_ptr->size;

    auto reader_result = pak_reader::open(mmap_ptr->span());
    if (!reader_result) {
        return std::unexpected(reader_result.error());
    }

    std::lock_guard lock(impl_->write_mutex);

    auto old = impl_->snapshot.load();

    for (const auto &m : old->mounts) {
        if (m->name == name) {
            return std::unexpected(error{error_code::already_mounted, std::format("mount '{}' already exists", name)});
        }
    }

    auto mp = std::make_shared<mount_point>();
    mp->name = std::string(name);
    mp->priority = priority;
    mp->mmap_data = std::move(mmap_ptr);
    mp->is_mmap = true;
    mp->reader = std::move(*reader_result);

    auto next = std::make_shared<vfs_snapshot>(*old);
    next->mounts.push_back(std::move(mp));
    next->rebuild();

    SMOOTHIE_STAT_INC(impl_->counters.mount_count);
    SMOOTHIE_STAT_ADD(impl_->counters.total_bytes, mmap_total_size);
    impl_->snapshot.store(std::move(next));
    return {};
}

auto vfs::unmount(std::string_view name) -> diagnostic_result<void> {
    std::lock_guard lock(impl_->write_mutex);

    auto old = impl_->snapshot.load();

    bool found = false;
    auto next = std::make_shared<vfs_snapshot>();
    for (const auto &m : old->mounts) {
        if (m->name == name && !found) {
            found = true;
            continue;
        }
        next->mounts.push_back(m);
    }

    if (!found) {
        return std::unexpected(error{error_code::not_mounted, std::format("mount '{}' not found", name)});
    }

    next->rebuild();
    impl_->snapshot.store(std::move(next));
    return {};
}

void vfs::rebuild_index() {
    std::lock_guard lock(impl_->write_mutex);

    auto old = impl_->snapshot.load();
    auto next = std::make_shared<vfs_snapshot>(*old);
    next->rebuild();
    impl_->snapshot.store(std::move(next));
}

auto vfs::get(uint64_t semantic_hash) const -> result<std::span<const std::byte>> {
    SMOOTHIE_STAT_INC(impl_->counters.get_count);
    auto snap = impl_->snapshot.load();

    auto idx = snap->index_hashes.find(semantic_hash);
    if (idx >= snap->index_hashes.size()) [[unlikely]] {
        SMOOTHIE_STAT_INC(impl_->counters.miss_count);
        return std::unexpected(error_code::not_found);
    }
    SMOOTHIE_STAT_INC(impl_->counters.hit_count);

    const auto &re = snap->index_entries[idx];
    const size_t abs_end = static_cast<size_t>(re.data_offset) + re.data_size;

    if (!re.data_base_ptr || abs_end > re.buf_total_size) [[unlikely]] {
        return std::unexpected(error_code::corrupted);
    }

    return std::span<const std::byte>(re.data_base_ptr + re.data_offset, re.data_size);
}

auto vfs::get_localized(uint64_t base_hash, std::string_view uri, std::string_view current_locale,
                        std::span<const std::string> fallback_chain) const -> result<std::span<const std::byte>> {
    if (!current_locale.empty()) {
        auto localized_hash = hash64_ns(current_locale, uri);
        auto r = get(localized_hash);
        if (r.has_value()) {
            return r;
        }
    }

    for (const auto &locale : fallback_chain) {
        auto localized_hash = hash64_ns(locale, uri);
        auto r = get(localized_hash);
        if (r.has_value()) {
            return r;
        }
    }

    return get(base_hash);
}

auto vfs::get_dynamic(std::string_view uri) const -> result<resource_view> {
    if (uri.starts_with("file://")) {
        auto file_path = std::filesystem::path(std::string(uri.substr(7)));
        std::ifstream in(file_path, std::ios::binary | std::ios::ate);
        if (!in) {
            return std::unexpected(error_code::io_error);
        }
        auto size = in.tellg();
        in.seekg(0);
        std::vector<std::byte> buf(static_cast<size_t>(size));
        in.read(reinterpret_cast<char *>(buf.data()), size);
        if (!in) {
            return std::unexpected(error_code::io_error);
        }
        return resource_view(std::move(buf));
    }

    if (uri.starts_with("res://")) {
        auto snap = impl_->snapshot.load();
        auto rest = uri.substr(6);

        auto slash_pos = rest.find('/');
        if (slash_pos != std::string_view::npos) {
            auto first_seg = rest.substr(0, slash_pos);

            auto nit = std::lower_bound(snap->name_index.begin(), snap->name_index.end(), first_seg,
                                        [](const auto &pair, std::string_view n) { return pair.first < n; });

            if (nit != snap->name_index.end() && nit->first == first_seg) {
                auto inner_path = rest.substr(slash_pos + 1);
                auto hash = hash64(inner_path);
                const auto &mp = *snap->mounts[nit->second];
                auto *entry = mp.reader.find(hash);
                if (entry) {
                    auto dr = mp.reader.data_of(*entry);
                    if (dr) {
                        return resource_view(*dr);
                    }
                }
                return std::unexpected(error_code::not_found);
            }
        }

        auto hash = hash64(rest);
        auto span_result = get(hash);
        if (span_result) {
            return resource_view(*span_result);
        }
        return std::unexpected(span_result.error());
    }

    std::ifstream in(std::filesystem::path(std::string(uri)), std::ios::binary | std::ios::ate);
    if (!in) {
        return std::unexpected(error_code::io_error);
    }
    auto size = in.tellg();
    in.seekg(0);
    std::vector<std::byte> buf(static_cast<size_t>(size));
    in.read(reinterpret_cast<char *>(buf.data()), size);
    if (!in) {
        return std::unexpected(error_code::io_error);
    }
    return resource_view(std::move(buf));
}

auto vfs::get_view(uint64_t semantic_hash) const -> diagnostic_result<resource_view> {
    auto snap = impl_->snapshot.load();

    auto idx = snap->index_hashes.find(semantic_hash);
    if (idx >= snap->index_hashes.size()) {
        return std::unexpected(error{error_code::not_found, "resource not found"});
    }

    const auto &re = snap->index_entries[idx];
    const size_t abs_end = static_cast<size_t>(re.data_offset) + re.data_size;

    if (!re.data_base_ptr || abs_end > re.buf_total_size) {
        return std::unexpected(error{error_code::corrupted, "data extends beyond buffer"});
    }

    auto raw = std::span<const std::byte>(re.data_base_ptr + re.data_offset, re.data_size);
    auto ef = static_cast<entry_flags>(re.flags);

    if (!is_compressed(ef)) {
        return resource_view(raw);
    }

    auto decompressed = decompress(raw, re.reserved, ef);
    if (!decompressed.has_value()) {
        return std::unexpected(decompressed.error());
    }

    return resource_view(std::move(*decompressed));
}

auto vfs::get_entry_info(uint64_t semantic_hash) const -> result<entry_descriptor> {
    auto snap = impl_->snapshot.load();
    auto idx = snap->index_hashes.find(semantic_hash);
    if (idx >= snap->index_hashes.size()) {
        return std::unexpected(error_code::not_found);
    }
    const auto &re = snap->index_entries[idx];
    return entry_descriptor{
        .semantic_hash = semantic_hash,
        .data_offset = re.data_offset,
        .data_size = re.data_size,
        .type = re.type,
        .flags = re.flags,
        .reserved = re.reserved,
    };
}

auto vfs::contains(uint64_t semantic_hash) const noexcept -> bool {
    auto snap = impl_->snapshot.load();
    return snap->index_hashes.find(semantic_hash) < snap->index_hashes.size();
}

auto vfs::mount_count() const noexcept -> size_t {
    auto snap = impl_->snapshot.load();
    return snap->mounts.size();
}

auto vfs::resource_count() const noexcept -> size_t {
    auto snap = impl_->snapshot.load();
    return snap->index_hashes.size();
}

auto vfs::mount_names() const -> std::vector<std::string> {
    auto snap = impl_->snapshot.load();
    std::vector<std::string> names;
    names.reserve(snap->mounts.size());
    for (const auto &m : snap->mounts) {
        names.emplace_back(m->name);
    }
    return names;
}

auto vfs::enumerate() const -> std::vector<uint64_t> {
    auto snap = impl_->snapshot.load();
    return snap->index_hashes.sorted_keys();
}

auto vfs::mount_info() const -> std::vector<mount_point_info> {
    auto snap = impl_->snapshot.load();
    std::vector<mount_point_info> infos;
    infos.reserve(snap->mounts.size());
    for (const auto &mp : snap->mounts) {
        mount_point_info info;
        info.name = mp->name;
        info.priority = mp->priority;
        info.resource_count = mp->reader.entry_count();
        info.data_size = mount_data(*mp).size();
        info.is_embedded = mp->is_embedded;
        info.is_mmap = mp->is_mmap;
        infos.push_back(std::move(info));
    }
    return infos;
}

auto vfs::get_view_localized(uint64_t base_hash, std::string_view uri, std::string_view current_locale,
                             std::span<const std::string> fallback_chain) const -> diagnostic_result<resource_view> {
    if (!current_locale.empty()) {
        auto localized_hash = hash64_ns(current_locale, uri);
        auto r = get_view(localized_hash);
        if (r.has_value()) {
            return r;
        }
    }

    for (const auto &locale : fallback_chain) {
        auto localized_hash = hash64_ns(locale, uri);
        auto r = get_view(localized_hash);
        if (r.has_value()) {
            return r;
        }
    }

    return get_view(base_hash);
}

auto vfs::watch(watch_callback cb) -> uint64_t {
    auto snap = impl_->snapshot.load();
    (void)cb;
    // TODO: integrate with frappe watcher for hot-reload
    return 0;
}

void vfs::unwatch(uint64_t token) {
    (void)token;
    // TODO: integrate with frappe watcher for hot-reload
}

// ── Performance counters ────────────────────────────────────────────

auto vfs::stats() const noexcept -> vfs_stats {
#ifdef SMOOTHIE_ENABLE_STATS
    return impl_->counters.snapshot();
#else
    return {};
#endif
}

void vfs::reset_stats() noexcept {
#ifdef SMOOTHIE_ENABLE_STATS
    impl_->counters.reset();
#endif
}

} // namespace smoothie::resource
