#pragma once

/**
 * @file exports.h
 * @brief DLL export/import macros for the smoothie library.
 */

#if defined(_WIN32) || defined(_WIN64)
#if defined(SMOOTHIE_BUILD_INTERNAL)
#define SMOOTHIE_API __declspec(dllexport)
#else
#define SMOOTHIE_API __declspec(dllimport)
#endif
#else
#define SMOOTHIE_API __attribute__((visibility("default")))
#endif
