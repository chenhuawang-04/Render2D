cmake_minimum_required(VERSION 3.28)

project(Render2D
    VERSION 0.1.0
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RENDER2D_BUILD_TESTS "Build Render2D tests" ON)
option(RENDER2D_BUILD_BENCHMARKS "Build Render2D benchmark smoke targets" OFF)
option(RENDER2D_REQUIRE_VULKAN "Require the Vulkan SDK at configure time" ON)
option(RENDER2D_BUILD_FONT_RUNTIME "Build the FreeType+HarfBuzz+SheenBidi font/text runtime support target (Stage 19)" ON)

set(RENDER2D_MEMORY_CENTER_SOURCE_DIR "E:/Project/MelosyneTest/MemoryCenterNew" CACHE PATH
    "Path to MemoryCenterNew source tree.")
set(RENDER2D_FAST_MATH_SOURCE_DIR "E:/Project/MelosyneTest/Math/fast_math" CACHE PATH
    "Path to fast_math source tree.")
set(RENDER2D_VECTOR_NEW_INCLUDE_DIR "E:/Project/MelosyneTest/Vector_New/include" CACHE PATH
    "Path to Vector_New include tree providing Center/Memory/Container/Vector/McVector.hpp.")
set(RENDER2D_THREAD_CENTER_SOURCE_DIR "E:/Project/MelosyneTest/ThreadCenter" CACHE PATH
    "Path to ThreadCenter source tree.")

if(NOT EXISTS "${RENDER2D_MEMORY_CENTER_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "MemoryCenterNew was not found at RENDER2D_MEMORY_CENTER_SOURCE_DIR='${RENDER2D_MEMORY_CENTER_SOURCE_DIR}'.")
endif()
if(NOT EXISTS "${RENDER2D_FAST_MATH_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "fast_math was not found at RENDER2D_FAST_MATH_SOURCE_DIR='${RENDER2D_FAST_MATH_SOURCE_DIR}'.")
endif()
if(NOT EXISTS "${RENDER2D_VECTOR_NEW_INCLUDE_DIR}/Center/Memory/Container/Vector/McVector.hpp")
    message(FATAL_ERROR "McVector was not found at RENDER2D_VECTOR_NEW_INCLUDE_DIR='${RENDER2D_VECTOR_NEW_INCLUDE_DIR}'.")
endif()
if(NOT EXISTS "${RENDER2D_THREAD_CENTER_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "ThreadCenter was not found at RENDER2D_THREAD_CENTER_SOURCE_DIR='${RENDER2D_THREAD_CENTER_SOURCE_DIR}'.")
endif()

set(MCENTER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MCENTER_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(MCENTER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MCENTER_ENABLE_CPPM OFF CACHE BOOL "" FORCE)
set(MCENTER_TEST_USE_CPPM OFF CACHE BOOL "" FORCE)
set(MCENTER_BENCH_USE_CPPM OFF CACHE BOOL "" FORCE)
set(MCENTER_INSTALL OFF CACHE BOOL "" FORCE)

set(FAST_MATH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FAST_MATH_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(FAST_MATH_BUILD_MODULES OFF CACHE BOOL "" FORCE)

set(THREAD_CENTER_BUILD_HEADER_API ON CACHE BOOL "" FORCE)
set(THREAD_CENTER_BUILD_MODULES OFF CACHE BOOL "" FORCE)
set(THREAD_CENTER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(THREAD_CENTER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(THREAD_CENTER_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(THREAD_CENTER_BUILD_TESTS_HEADER OFF CACHE BOOL "" FORCE)
set(THREAD_CENTER_BUILD_TESTS_MODULE OFF CACHE BOOL "" FORCE)
set(THREAD_CENTER_BUILD_BENCH_HEADER OFF CACHE BOOL "" FORCE)
set(THREAD_CENTER_BUILD_BENCH_MODULE OFF CACHE BOOL "" FORCE)
set(THREAD_CENTER_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory("${RENDER2D_MEMORY_CENTER_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/MemoryCenterNew")
add_subdirectory("${RENDER2D_FAST_MATH_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/fast_math")
add_subdirectory("${RENDER2D_THREAD_CENTER_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/ThreadCenter")

get_target_property(RENDER2D_THREAD_CENTER_INCLUDE_DIRS
    Center.Thread.Headers
    INTERFACE_INCLUDE_DIRECTORIES
)
if(RENDER2D_THREAD_CENTER_INCLUDE_DIRS)
    set_property(TARGET Center.Thread.Headers PROPERTY
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${RENDER2D_THREAD_CENTER_INCLUDE_DIRS}"
    )
endif()

if(RENDER2D_REQUIRE_VULKAN)
    find_package(Vulkan REQUIRED)
else()
    find_package(Vulkan QUIET)
endif()

add_library(Render2D INTERFACE)
add_library(Render2D::Render2D ALIAS Render2D)
add_library(render2d_thread_runtime_support INTERFACE)

target_compile_features(Render2D INTERFACE cxx_std_23)
target_include_directories(Render2D INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${RENDER2D_VECTOR_NEW_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(Render2D INTERFACE
    Center.Memory.Headers
    fast_math
)

target_link_libraries(render2d_thread_runtime_support INTERFACE
    Render2D::Render2D
    Center.Thread.Headers
)

if(TARGET Vulkan::Vulkan)
    target_link_libraries(Render2D INTERFACE Vulkan::Vulkan)
    target_link_libraries(render2d_thread_runtime_support INTERFACE Vulkan::Vulkan)
endif()

# Stage 19 font/text runtime support. FreeType, HarfBuzz, and SheenBidi build as
# static libraries behind the internal render2d_font_runtime_support target and
# are NEVER included from the public umbrella Render2D.hpp -- the same isolation
# pattern as render2d_thread_runtime_support. Code that needs real shaping
# (e.g. include/Render2D/Native/VulkanFontRuntime.hpp) links this target.
if(RENDER2D_BUILD_FONT_RUNTIME)
    # FreeType: self-contained. Disable optional compression/image backends and
    # break the FreeType<->HarfBuzz auto-hinting cycle (HarfBuzz needs FreeType,
    # not the reverse).
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)

    # HarfBuzz: core shaping + FreeType interop only. No subset/raster/vector/gpu
    # tools and no glib/icu/gobject/introspection. HB_HAVE_FREETYPE auto-enables
    # once the freetype target exists (added first, below).
    set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_RASTER OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_VECTOR OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_GPU OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
    set(HB_HAVE_GLIB OFF CACHE BOOL "" FORCE)
    set(HB_HAVE_ICU OFF CACHE BOOL "" FORCE)
    set(HB_HAVE_GOBJECT OFF CACHE BOOL "" FORCE)
    set(HB_HAVE_INTROSPECTION OFF CACHE BOOL "" FORCE)

    # SheenBidi: unity build, no generator tool.
    set(SB_CONFIG_UNITY ON CACHE BOOL "" FORCE)
    set(BUILD_GENERATOR OFF CACHE BOOL "" FORCE)

    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype" "${CMAKE_CURRENT_BINARY_DIR}/_deps/freetype")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz" "${CMAKE_CURRENT_BINARY_DIR}/_deps/harfbuzz")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/sheenbidi" "${CMAKE_CURRENT_BINARY_DIR}/_deps/sheenbidi")

    add_library(render2d_font_runtime_support INTERFACE)
    target_link_libraries(render2d_font_runtime_support INTERFACE
        Render2D::Render2D
        freetype
        harfbuzz
        SheenBidi::SheenBidi
    )
    if(TARGET Vulkan::Vulkan)
        target_link_libraries(render2d_font_runtime_support INTERFACE Vulkan::Vulkan)
    endif()

    # Treat the third-party font headers as system includes so their warnings do
    # not trip Render2D's -Werror when our runtime headers include them (same
    # treatment as the ThreadCenter headers above).
    foreach(render2d_font_lib IN ITEMS freetype harfbuzz SheenBidi)
        if(TARGET ${render2d_font_lib})
            get_target_property(render2d_font_lib_includes ${render2d_font_lib} INTERFACE_INCLUDE_DIRECTORIES)
            if(render2d_font_lib_includes)
                set_property(TARGET ${render2d_font_lib} PROPERTY
                    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${render2d_font_lib_includes}"
                )
            endif()
        endif()
    endforeach()
endif()

function(render2d_apply_strict_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
        )
    else()
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
        )
    endif()
endfunction()

if(RENDER2D_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(RENDER2D_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()
