#pragma once

/// @file smoothie_c_internal.h
/// @brief Internal opaque struct definitions for the C API.

#include "smoothie/resource/pak_writer.h"
#include "smoothie/resource/vfs.h"

struct smoothie_vfs {
    smoothie::resource::vfs inner;
};

struct smoothie_pak_writer {
    smoothie::resource::pak_writer inner;
};
