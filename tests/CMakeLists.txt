# ── Test targets for smoothie ─────────────────────────────────────────────

function(smoothie_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE smoothie gtest gtest_main)
    set_target_properties(${name} PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/code/bin"
    )
    add_test(NAME ${name} COMMAND ${name})

    # Copy smoothie.dll next to test exe (Windows)
    if(WIN32 AND TARGET smoothie AND NOT BUILD_SHARED_LIBS STREQUAL "OFF")
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:smoothie> $<TARGET_FILE_DIR:${name}>
            COMMENT "[${name}] Copying smoothie.dll"
        )
        # Copy compression DLLs from CommonDependencies
        set(_COMMON_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/../../CommonDependencies")
        foreach(_dll lz4 zstd)
            set(_dll_path "${_COMMON_DEPS}/${_dll}/${COCA_TARGET_PROFILE}/bin/${_dll}.dll")
            if(EXISTS "${_dll_path}")
                add_custom_command(TARGET ${name} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_dll_path}" $<TARGET_FILE_DIR:${name}>
                    COMMENT "[${name}] Copying ${_dll}.dll"
                )
            endif()
        endforeach()
    endif()
endfunction()

smoothie_add_test(smoothie_eytzinger_test        eytzinger_test.cpp)
smoothie_add_test(smoothie_compression_test      compression_test.cpp)
smoothie_add_test(smoothie_pak_roundtrip_test    pak_roundtrip_test.cpp)
smoothie_add_test(smoothie_types_test            types_test.cpp)
smoothie_add_test(smoothie_vfs_test              vfs_test.cpp)
smoothie_add_test(smoothie_c_api_test            smoothie_c_test.cpp)
smoothie_add_test(smoothie_mphf_test             mphf_test.cpp)
