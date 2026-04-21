/**
 * @file smoothie_c.h
 * @brief Stable C API for the smoothie asset management library.
 *
 * Pure C99 header — opaque handle API for FFI consumers
 * (Python, C#, Lua, Go, WebAssembly, etc.).
 *
 * All functions use the SMOOTHIE_CAPI calling convention and return
 * smoothie_error_code on failure. String outputs are written into
 * caller-provided buffers with explicit length parameters.
 */

#ifndef SMOOTHIE_C_H
#define SMOOTHIE_C_H

#include <stddef.h>
#include <stdint.h>

/* ── ABI version ──────────────────────────────────────────────────── */

#define SMOOTHIE_C_API_VERSION_MAJOR 1
#define SMOOTHIE_C_API_VERSION_MINOR 0
#define SMOOTHIE_C_API_VERSION ((SMOOTHIE_C_API_VERSION_MAJOR << 16) | SMOOTHIE_C_API_VERSION_MINOR)

/* ── Export / calling convention ───────────────────────────────────── */

#if defined(SMOOTHIE_BUILD_INTERNAL)
#    if defined(_MSC_VER)
#        define SMOOTHIE_CAPI __declspec(dllexport)
#    elif defined(__GNUC__) || defined(__clang__)
#        define SMOOTHIE_CAPI __attribute__((visibility("default")))
#    else
#        define SMOOTHIE_CAPI
#    endif
#else
#    if defined(_MSC_VER)
#        define SMOOTHIE_CAPI __declspec(dllimport)
#    else
#        define SMOOTHIE_CAPI
#    endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ── Error codes ──────────────────────────────────────────────────── */

typedef enum smoothie_error_code {
    SMOOTHIE_OK = 0,
    SMOOTHIE_ERROR_NOT_FOUND = 1,
    SMOOTHIE_ERROR_CORRUPTED = 2,
    SMOOTHIE_ERROR_MMAP_FAILED = 3,
    SMOOTHIE_ERROR_VERSION_MISMATCH = 4,
    SMOOTHIE_ERROR_ALREADY_MOUNTED = 5,
    SMOOTHIE_ERROR_NOT_MOUNTED = 6,
    SMOOTHIE_ERROR_IO = 9,
    SMOOTHIE_ERROR_INVALID_ARGUMENT = 10,
    SMOOTHIE_ERROR_BUFFER_TOO_SMALL = 11,
    SMOOTHIE_ERROR_NULL_POINTER = 12,
} smoothie_error_code;

/** Return a static string describing the error code. */
SMOOTHIE_CAPI const char *smoothie_error_message(smoothie_error_code code);

/* ── Resource types ───────────────────────────────────────────────── */

typedef enum smoothie_resource_type {
    SMOOTHIE_RESOURCE_UNKNOWN = 0,
    SMOOTHIE_RESOURCE_IMAGE = 1,
    SMOOTHIE_RESOURCE_FONT = 2,
    SMOOTHIE_RESOURCE_I18N = 3,
    SMOOTHIE_RESOURCE_BYTECODE = 4,
    SMOOTHIE_RESOURCE_RAW = 5,
    SMOOTHIE_RESOURCE_SHADER = 6,
    SMOOTHIE_RESOURCE_MODEL = 7,
    SMOOTHIE_RESOURCE_MATERIAL = 8,
    SMOOTHIE_RESOURCE_SCENE = 9,
    SMOOTHIE_RESOURCE_UI_LAYOUT = 10,
    SMOOTHIE_RESOURCE_CONFIG = 11,
    SMOOTHIE_RESOURCE_AUDIO = 12,
    SMOOTHIE_RESOURCE_DATA = 13,
} smoothie_resource_type;

/* ── Compression modes ────────────────────────────────────────────── */

typedef enum smoothie_compression_mode {
    SMOOTHIE_COMPRESS_NONE = 0,
    SMOOTHIE_COMPRESS_LZ4 = 1,
    SMOOTHIE_COMPRESS_ZSTD = 2,
} smoothie_compression_mode;

/* ── Version query ────────────────────────────────────────────────── */

/** Return the C API version (SMOOTHIE_C_API_VERSION). */
SMOOTHIE_CAPI uint32_t smoothie_api_version(void);

/* ═══════════════════════════════════════════════════════════════════
 *  VFS API
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct smoothie_vfs smoothie_vfs;

/** Create a new VFS instance. Caller must destroy with smoothie_vfs_destroy(). */
SMOOTHIE_CAPI smoothie_error_code smoothie_vfs_create(smoothie_vfs **out_vfs);

/** Destroy a VFS instance. */
SMOOTHIE_CAPI void smoothie_vfs_destroy(smoothie_vfs *vfs);

/** Mount a .lpak file from disk. */
SMOOTHIE_CAPI smoothie_error_code smoothie_vfs_mount(smoothie_vfs *vfs, const char *name, const char *path,
                                                     int priority);

/** Mount from an in-memory buffer (data is copied). */
SMOOTHIE_CAPI smoothie_error_code smoothie_vfs_mount_memory(smoothie_vfs *vfs, const char *name, const void *data,
                                                            size_t data_size, int priority);

/** Unmount a previously mounted package by name. */
SMOOTHIE_CAPI smoothie_error_code smoothie_vfs_unmount(smoothie_vfs *vfs, const char *name);

/**
 * Look up a resource by semantic hash.
 * On success, *out_data points into the mounted .lpak memory (zero-copy)
 * and *out_size is set. The pointer is valid until the VFS is modified.
 * out_data and out_size must not be NULL.
 */
SMOOTHIE_CAPI smoothie_error_code smoothie_vfs_get(const smoothie_vfs *vfs, uint64_t semantic_hash,
                                                   const void **out_data, size_t *out_size);

/** Check if a resource exists. Returns 1 if found, 0 otherwise. */
SMOOTHIE_CAPI int smoothie_vfs_contains(const smoothie_vfs *vfs, uint64_t semantic_hash);

/** Return the number of mounted packages. */
SMOOTHIE_CAPI size_t smoothie_vfs_mount_count(const smoothie_vfs *vfs);

/** Return the total number of resources. */
SMOOTHIE_CAPI size_t smoothie_vfs_resource_count(const smoothie_vfs *vfs);

/* ═══════════════════════════════════════════════════════════════════
 *  pak_writer API
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct smoothie_pak_writer smoothie_pak_writer;

/** Create a new pak_writer instance. Caller must destroy with smoothie_pak_writer_destroy(). */
SMOOTHIE_CAPI smoothie_error_code smoothie_pak_writer_create(smoothie_pak_writer **out_writer);

/** Destroy a pak_writer instance. */
SMOOTHIE_CAPI void smoothie_pak_writer_destroy(smoothie_pak_writer *writer);

/** Add a resource entry. data is copied internally. */
SMOOTHIE_CAPI smoothie_error_code smoothie_pak_writer_add(smoothie_pak_writer *writer, const char *path,
                                                          smoothie_resource_type type, const void *data,
                                                          size_t data_size);

/** Set the default compression mode for subsequently added entries. */
SMOOTHIE_CAPI void smoothie_pak_writer_set_compression(smoothie_pak_writer *writer, smoothie_compression_mode mode);

/** Write the .lpak file to disk. */
SMOOTHIE_CAPI smoothie_error_code smoothie_pak_writer_write(const smoothie_pak_writer *writer, const char *output_path);

/** Return the number of entries added. */
SMOOTHIE_CAPI size_t smoothie_pak_writer_entry_count(const smoothie_pak_writer *writer);

/* ═══════════════════════════════════════════════════════════════════
 *  Hashing utility
 * ═══════════════════════════════════════════════════════════════════ */

/** Compute the 64-bit FNV-1a hash of a string (same as C++ hash64). */
SMOOTHIE_CAPI uint64_t smoothie_hash64(const char *str, size_t len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SMOOTHIE_C_H */
