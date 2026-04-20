# ── Smoothie Performance Benchmarks (Google Benchmark + VTune) ────────────────
#
# Targets:
#   bench_smoothie        — benchmark executable
#   vtune_bench_smoothie  — VTune hotspots profiling target
#
# Usage:
#   cmake --build <dir> --target bench_smoothie
#   ./code/bin/bench_smoothie --benchmark_format=console
#   cmake --build <dir> --target vtune_bench_smoothie

if(NOT TARGET COCA::benchmark)
    message(STATUS "[smoothie-bench] COCA::benchmark not found — skipping benchmarks")
    return()
endif()

if(NOT TARGET smoothie)
    message(STATUS "[smoothie-bench] smoothie target not found — skipping benchmarks")
    return()
endif()

set(SMOOTHIE_BENCH_SOURCES
    bench_hash.cpp
    bench_compression.cpp
    bench_vfs.cpp
    bench_pak_reader.cpp
)

add_executable(bench_smoothie ${SMOOTHIE_BENCH_SOURCES})

target_link_libraries(bench_smoothie PRIVATE
    smoothie::smoothie
    COCA::benchmark
)

set_target_properties(bench_smoothie PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/code/bin"
)

target_compile_options(bench_smoothie PRIVATE -Wall -Wextra -Wpedantic)

# Copy dependency DLLs next to benchmark exe (Windows)
if(WIN32)
    foreach(_dll smoothie lz4 zstd)
        if(_dll STREQUAL "smoothie")
            if(TARGET smoothie)
                add_custom_command(TARGET bench_smoothie POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:smoothie> $<TARGET_FILE_DIR:bench_smoothie>
                    COMMENT "[bench_smoothie] Copying smoothie.dll"
                )
            endif()
        else()
            set(_dll_path "${_COMMON_DEPS}/${_dll}/${COCA_TARGET_PROFILE}/bin/${_dll}.dll")
            if(EXISTS "${_dll_path}")
                add_custom_command(TARGET bench_smoothie POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_dll_path}" $<TARGET_FILE_DIR:bench_smoothie>
                    COMMENT "[bench_smoothie] Copying ${_dll}.dll"
                )
            endif()
        endif()
    endforeach()
endif()

# ── VTune profiling target ────────────────────────────────────────────────────
set(_vtune_cmake "${COCA_TOOLCHAIN_ROOT}/cmake/coca_vtune.cmake")
if(NOT DEFINED COCA_TOOLCHAIN_ROOT)
    get_filename_component(COCA_TOOLCHAIN_ROOT "${CMAKE_C_COMPILER}/../.." ABSOLUTE)
    set(_vtune_cmake "${COCA_TOOLCHAIN_ROOT}/cmake/coca_vtune.cmake")
endif()
if(EXISTS "${_vtune_cmake}")
    include("${_vtune_cmake}")
    if(COCA_VTUNE_FOUND)
        coca_vtune_profile(
            TARGET bench_smoothie
            ANALYSIS_TYPE hotspots
            DURATION 30
        )
        coca_vtune_report(
            TARGET bench_smoothie
            REPORT_TYPE hotspots
            OUTPUT_FILE "${CMAKE_BINARY_DIR}/vtune_results/bench_smoothie_hotspots.txt"
        )
        message(STATUS "[smoothie-bench] VTune targets: vtune_bench_smoothie, vtune_report_bench_smoothie")
    endif()
endif()
