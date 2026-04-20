# smoothie — Asset management: VFS, .lpak reader/writer, compression, MPHF (COCA workspace build)
# Provides: smoothie (SHARED), smoothie::smoothie
# Dependencies: COCA::lz4, COCA::zstd

# ── Guard: skip if required COCA:: deps are not available ────────────
foreach(_dep IN ITEMS COCA::lz4 COCA::zstd)
    if(NOT TARGET ${_dep})
        message(STATUS "[smoothie] Skipping — ${_dep} not available")
        add_library(smoothie INTERFACE)
        add_library(smoothie::smoothie ALIAS smoothie)
        target_include_directories(smoothie INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
        return()
    endif()
endforeach()

set(SMOOTHIE_VERSION_MAJOR 0)
set(SMOOTHIE_VERSION_MINOR 1)
set(SMOOTHIE_VERSION_PATCH 0)
set(smoothie_VERSION "0.1.0")

option(SMOOTHIE_ENABLE_COMPRESSION "Enable LZ4/Zstd block compression" ON)
option(SMOOTHIE_USE_MPHF "Use O(1) MPHF lookup in pak_reader" ON)
option(SMOOTHIE_DEFAULT_SINGLE_THREAD "Default to single-thread mode" OFF)
option(SMOOTHIE_ENABLE_STATS "Enable VFS performance counters" OFF)

# ── Sources ──────────────────────────────────────────────────────────
set(SMOOTHIE_SOURCES
    src/types.cpp
    src/threading.cpp
    src/smoothie_c.cpp
    src/resource/compression.cpp
    src/resource/pak_reader.cpp
    src/resource/pak_writer.cpp
    src/resource/vfs.cpp
    src/resource/asset_manager.cpp
)

# ── Shared library ───────────────────────────────────────────────────
if(EMSCRIPTEN)
    add_library(smoothie STATIC ${SMOOTHIE_SOURCES})
else()
    add_library(smoothie SHARED ${SMOOTHIE_SOURCES})
endif()
add_library(smoothie::smoothie ALIAS smoothie)

target_include_directories(smoothie
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(smoothie PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_compile_definitions(smoothie PRIVATE SMOOTHIE_BUILD_INTERNAL)

if(SMOOTHIE_USE_MPHF)
    target_compile_definitions(smoothie PUBLIC SMOOTHIE_USE_MPHF)
endif()

if(SMOOTHIE_DEFAULT_SINGLE_THREAD)
    target_compile_definitions(smoothie PUBLIC SMOOTHIE_SINGLE_THREAD)
endif()

if(SMOOTHIE_ENABLE_STATS)
    target_compile_definitions(smoothie PUBLIC SMOOTHIE_ENABLE_STATS)
endif()

if(MSVC)
    target_compile_definitions(smoothie PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(smoothie PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(smoothie PRIVATE
        $<$<CONFIG:Release>:/Oi /GS- /fp:fast /GL>
        $<$<CONFIG:RelWithDebInfo>:/Oi /fp:fast>
    )
    target_link_options(smoothie PRIVATE
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
    )
else()
    target_compile_options(smoothie PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ── Compression library linking (COCA pre-built) ─────────────────────
if(SMOOTHIE_ENABLE_COMPRESSION)
    target_compile_definitions(smoothie PUBLIC SMOOTHIE_HAS_COMPRESSION)
    target_link_libraries(smoothie PRIVATE COCA::lz4 COCA::zstd)

    # Copy DLLs next to smoothie.dll at build time (Windows only)
    if(WIN32)
        # _COCA_ARCH_BIN is set by lz4/zstd build.cmake via _coca_select_arch
        foreach(_dll lz4 zstd)
            set(_dll_path "${_COMMON_DEPS}/${_dll}/${COCA_TARGET_PROFILE}/bin/${_dll}.dll")
            if(EXISTS "${_dll_path}")
                add_custom_command(TARGET smoothie POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_dll_path}" $<TARGET_FILE_DIR:smoothie>
                    COMMENT "[smoothie] Copying ${_dll}.dll"
                )
            endif()
        endforeach()
    endif()
endif()

# ── frappe (available in workspace) ──────────────────────────────────
if(TARGET frappe::frappe)
    target_link_libraries(smoothie PRIVATE frappe::frappe)
    target_compile_definitions(smoothie PUBLIC SMOOTHIE_HAS_FS_LIB)
endif()

# ── Tests (opt-in via GTest) ────────────────────────────────────────────
if(TARGET gtest AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()

# ── Benchmarks (opt-in via COCA::benchmark) ─────────────────────────────
if(TARGET COCA::benchmark AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/CMakeLists.txt")
    add_subdirectory(benchmarks)
endif()
