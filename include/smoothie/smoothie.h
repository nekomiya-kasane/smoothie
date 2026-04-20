#pragma once

/// @file smoothie.h
/// @brief Umbrella header for the smoothie asset management library.

#include "smoothie/exports.h"
#include "smoothie/types.h"

namespace smoothie {

/// Returns the library version as a string (e.g. "0.1.0").
[[nodiscard]] SMOOTHIE_API const char* version_string() noexcept;

/// Returns the library version as an integer (major * 10000 + minor * 100 + patch).
[[nodiscard]] SMOOTHIE_API int version_int() noexcept;

}  // namespace smoothie
