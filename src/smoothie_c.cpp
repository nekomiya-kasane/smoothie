/// @file smoothie_c.cpp
/// @brief C API implementation — thin wrappers around C++ classes.

#include "smoothie/smoothie_c.h"
#include "smoothie_c_internal.h"

#include "smoothie/resource/hash.h"
#include "smoothie/types.h"

#include <cstring>
#include <filesystem>
#include <new>
#include <string>

// ── Helpers ──────────────────────────────────────────────────────────

namespace {

smoothie_error_code map_error(smoothie::error_code ec) {
    switch (ec) {
    case smoothie::error_code::ok:                return SMOOTHIE_OK;
    case smoothie::error_code::not_found:         return SMOOTHIE_ERROR_NOT_FOUND;
    case smoothie::error_code::corrupted:         return SMOOTHIE_ERROR_CORRUPTED;
    case smoothie::error_code::mmap_failed:       return SMOOTHIE_ERROR_MMAP_FAILED;
    case smoothie::error_code::version_mismatch:  return SMOOTHIE_ERROR_VERSION_MISMATCH;
    case smoothie::error_code::already_mounted:   return SMOOTHIE_ERROR_ALREADY_MOUNTED;
    case smoothie::error_code::not_mounted:       return SMOOTHIE_ERROR_NOT_MOUNTED;
    case smoothie::error_code::io_error:          return SMOOTHIE_ERROR_IO;
    case smoothie::error_code::invalid_argument:  return SMOOTHIE_ERROR_INVALID_ARGUMENT;
    default:                                      return SMOOTHIE_ERROR_CORRUPTED;
    }
}

smoothie_error_code map_diagnostic_error(const smoothie::error& e) {
    return map_error(e.code);
}

}  // namespace

// ── Error messages ───────────────────────────────────────────────────

extern "C" {

const char* smoothie_error_message(smoothie_error_code code) {
    switch (code) {
    case SMOOTHIE_OK:                       return "ok";
    case SMOOTHIE_ERROR_NOT_FOUND:          return "not found";
    case SMOOTHIE_ERROR_CORRUPTED:          return "corrupted data";
    case SMOOTHIE_ERROR_MMAP_FAILED:        return "memory map failed";
    case SMOOTHIE_ERROR_VERSION_MISMATCH:   return "version mismatch";
    case SMOOTHIE_ERROR_ALREADY_MOUNTED:    return "already mounted";
    case SMOOTHIE_ERROR_NOT_MOUNTED:        return "not mounted";
    case SMOOTHIE_ERROR_IO:                 return "I/O error";
    case SMOOTHIE_ERROR_INVALID_ARGUMENT:   return "invalid argument";
    case SMOOTHIE_ERROR_BUFFER_TOO_SMALL:   return "buffer too small";
    case SMOOTHIE_ERROR_NULL_POINTER:       return "null pointer";
    default:                                return "unknown error";
    }
}

// ── Version ──────────────────────────────────────────────────────────

uint32_t smoothie_api_version(void) {
    return SMOOTHIE_C_API_VERSION;
}

// ═════════════════════════════════════════════════════════════════════
//  VFS API
// ═════════════════════════════════════════════════════════════════════

smoothie_error_code smoothie_vfs_create(smoothie_vfs** out_vfs) {
    if (!out_vfs) return SMOOTHIE_ERROR_NULL_POINTER;
    auto* v = new (std::nothrow) smoothie_vfs;
    if (!v) return SMOOTHIE_ERROR_IO;
    *out_vfs = v;
    return SMOOTHIE_OK;
}

void smoothie_vfs_destroy(smoothie_vfs* vfs) {
    delete vfs;
}

smoothie_error_code smoothie_vfs_mount(
    smoothie_vfs* vfs, const char* name, const char* path, int priority)
{
    if (!vfs || !name || !path) return SMOOTHIE_ERROR_NULL_POINTER;
    auto r = vfs->inner.mount(name, std::filesystem::path(path), priority);
    if (!r.has_value()) return map_diagnostic_error(r.error());
    return SMOOTHIE_OK;
}

smoothie_error_code smoothie_vfs_mount_memory(
    smoothie_vfs* vfs, const char* name, const void* data, size_t data_size, int priority)
{
    if (!vfs || !name) return SMOOTHIE_ERROR_NULL_POINTER;
    if (!data && data_size > 0) return SMOOTHIE_ERROR_NULL_POINTER;
    auto bytes = std::vector<std::byte>(
        static_cast<const std::byte*>(data),
        static_cast<const std::byte*>(data) + data_size);
    auto r = vfs->inner.mount(name, std::move(bytes), priority);
    if (!r.has_value()) return map_diagnostic_error(r.error());
    return SMOOTHIE_OK;
}

smoothie_error_code smoothie_vfs_unmount(smoothie_vfs* vfs, const char* name) {
    if (!vfs || !name) return SMOOTHIE_ERROR_NULL_POINTER;
    auto r = vfs->inner.unmount(name);
    if (!r.has_value()) return map_diagnostic_error(r.error());
    return SMOOTHIE_OK;
}

smoothie_error_code smoothie_vfs_get(
    const smoothie_vfs* vfs, uint64_t semantic_hash,
    const void** out_data, size_t* out_size)
{
    if (!vfs || !out_data || !out_size) return SMOOTHIE_ERROR_NULL_POINTER;
    auto r = vfs->inner.get(semantic_hash);
    if (!r.has_value()) return map_error(r.error());
    *out_data = r->data();
    *out_size = r->size();
    return SMOOTHIE_OK;
}

int smoothie_vfs_contains(const smoothie_vfs* vfs, uint64_t semantic_hash) {
    if (!vfs) return 0;
    return vfs->inner.contains(semantic_hash) ? 1 : 0;
}

size_t smoothie_vfs_mount_count(const smoothie_vfs* vfs) {
    if (!vfs) return 0;
    return vfs->inner.mount_count();
}

size_t smoothie_vfs_resource_count(const smoothie_vfs* vfs) {
    if (!vfs) return 0;
    return vfs->inner.resource_count();
}

// ═════════════════════════════════════════════════════════════════════
//  pak_writer API
// ═════════════════════════════════════════════════════════════════════

smoothie_error_code smoothie_pak_writer_create(smoothie_pak_writer** out_writer) {
    if (!out_writer) return SMOOTHIE_ERROR_NULL_POINTER;
    auto* w = new (std::nothrow) smoothie_pak_writer;
    if (!w) return SMOOTHIE_ERROR_IO;
    *out_writer = w;
    return SMOOTHIE_OK;
}

void smoothie_pak_writer_destroy(smoothie_pak_writer* writer) {
    delete writer;
}

smoothie_error_code smoothie_pak_writer_add(
    smoothie_pak_writer* writer, const char* path,
    smoothie_resource_type type, const void* data, size_t data_size)
{
    if (!writer || !path) return SMOOTHIE_ERROR_NULL_POINTER;
    if (!data && data_size > 0) return SMOOTHIE_ERROR_NULL_POINTER;
    auto span = std::span<const std::byte>(
        static_cast<const std::byte*>(data), data_size);
    writer->inner.add(path,
                      static_cast<smoothie::resource::resource_type>(type),
                      span);
    return SMOOTHIE_OK;
}

void smoothie_pak_writer_set_compression(
    smoothie_pak_writer* writer, smoothie_compression_mode mode)
{
    if (!writer) return;
    writer->inner.set_default_compression(
        static_cast<smoothie::resource::compression_mode>(mode));
}

smoothie_error_code smoothie_pak_writer_write(
    const smoothie_pak_writer* writer, const char* output_path)
{
    if (!writer || !output_path) return SMOOTHIE_ERROR_NULL_POINTER;
    auto r = writer->inner.write(std::filesystem::path(output_path));
    if (!r.has_value()) return map_diagnostic_error(r.error());
    return SMOOTHIE_OK;
}

size_t smoothie_pak_writer_entry_count(const smoothie_pak_writer* writer) {
    if (!writer) return 0;
    return writer->inner.entry_count();
}

// ═════════════════════════════════════════════════════════════════════
//  Hashing utility
// ═════════════════════════════════════════════════════════════════════

uint64_t smoothie_hash64(const char* str, size_t len) {
    if (!str) return 0;
    return smoothie::resource::hash64(std::string_view(str, len));
}

}  // extern "C"
