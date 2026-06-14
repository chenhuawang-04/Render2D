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

# ----------------------------------------------------------------------------
# Engine dependencies (host-engine source trees, NOT vendored in this repo).
#
# Each dependency is resolved in three tiers, downloading only as a last resort:
#   1. Reuse  - if the enclosing project already defines the dependency's target
#               (host-engine merge), Render2D reuses it untouched.
#   2. Local  - if a local source tree exists (the umbrella / individual paths
#               below), it is add_subdirectory()'d -- no download.
#   3. Fetch  - otherwise FetchContent pulls it from git (see the *_GIT_*
#               variables). Two of the four repos are private, so a fetch on a
#               clean / CI machine needs git credentials with access to them.
#
# Point the build at a local checkout with ONE umbrella path:
#     -DRENDER2D_ENGINE_DEPS_ROOT=/path/to/MelosyneTest
# or override any individual tree (takes precedence over the umbrella default):
#     -DRENDER2D_FAST_MATH_SOURCE_DIR=/custom/fast_math
#
# Expected layout under RENDER2D_ENGINE_DEPS_ROOT (see README.md > Dependencies):
#     <root>/MemoryCenterNew/        marker: CMakeLists.txt
#     <root>/Math/fast_math/         marker: CMakeLists.txt
#     <root>/Vector_New/include/     marker: Center/Memory/Container/Vector/McVector.hpp
#     <root>/ThreadCenter/           marker: CMakeLists.txt
# ----------------------------------------------------------------------------
set(RENDER2D_ENGINE_DEPS_ROOT "E:/Project/MelosyneTest" CACHE PATH
    "Root directory containing the four engine dependency source trees. Used to derive the per-dependency defaults below; each may also be overridden individually.")

set(RENDER2D_MEMORY_CENTER_SOURCE_DIR "${RENDER2D_ENGINE_DEPS_ROOT}/MemoryCenterNew" CACHE PATH
    "Path to MemoryCenterNew source tree.")
set(RENDER2D_FAST_MATH_SOURCE_DIR "${RENDER2D_ENGINE_DEPS_ROOT}/Math/fast_math" CACHE PATH
    "Path to fast_math source tree.")
set(RENDER2D_VECTOR_NEW_INCLUDE_DIR "${RENDER2D_ENGINE_DEPS_ROOT}/Vector_New/include" CACHE PATH
    "Path to Vector_New include tree providing Center/Memory/Container/Vector/McVector.hpp.")
set(RENDER2D_THREAD_CENTER_SOURCE_DIR "${RENDER2D_ENGINE_DEPS_ROOT}/ThreadCenter" CACHE PATH
    "Path to ThreadCenter source tree.")

# Git sources used by the fetch tier when no target or local tree is found.
# Each repo/tag is overridable so CI can pin an exact commit instead of a branch.
set(RENDER2D_MEMORY_CENTER_GIT_REPOSITORY "https://github.com/chenhuawang-04/MelosyneMemoryCenter.git" CACHE STRING
    "Git URL fetched for MemoryCenterNew when no target/local tree is available (private repo).")
set(RENDER2D_MEMORY_CENTER_GIT_TAG "master" CACHE STRING "Git ref fetched for MemoryCenterNew.")
set(RENDER2D_FAST_MATH_GIT_REPOSITORY "https://github.com/chenhuawang-04/Melosyne-Math.git" CACHE STRING
    "Git URL fetched for fast_math when no target/local tree is available.")
set(RENDER2D_FAST_MATH_GIT_TAG "master" CACHE STRING "Git ref fetched for fast_math.")
set(RENDER2D_VECTOR_NEW_GIT_REPOSITORY "https://github.com/chenhuawang-04/Vector.git" CACHE STRING
    "Git URL fetched for Vector_New (McVector) when no local include tree is available (private repo).")
set(RENDER2D_VECTOR_NEW_GIT_TAG "master" CACHE STRING "Git ref fetched for Vector_New (McVector).")
set(RENDER2D_THREAD_CENTER_GIT_REPOSITORY "https://github.com/chenhuawang-04/Melosyne_ThreadCenter.git" CACHE STRING
    "Git URL fetched for ThreadCenter when no target/local tree is available.")
set(RENDER2D_THREAD_CENTER_GIT_TAG "master" CACHE STRING "Git ref fetched for ThreadCenter.")

include(FetchContent)
# Show clone/populate progress in CI logs (helps diagnose fetch failures).
set(FETCHCONTENT_QUIET OFF)

# Resolve a subdirectory-style engine dependency in three tiers: reuse an
# existing target, else add_subdirectory a local tree, else FetchContent it from
# git. Defined as a macro so add_subdirectory()/FetchContent run in this
# (top-level) scope, exactly as the prior inline add_subdirectory() calls did.
# The resolved source tree is written back into out_source_dir_var so downstream
# consumers (e.g. the negative-compile test's manual -I wiring) use the actual
# location -- the fetched path when a dependency was downloaded.
macro(render2d_provide_engine_dependency dep_name target_name local_dir bin_subdir git_repo git_tag out_source_dir_var)
    if(TARGET ${target_name})
        message(STATUS "Render2D: reusing existing target '${target_name}' for ${dep_name} (no fetch).")
    elseif(EXISTS "${local_dir}/CMakeLists.txt")
        message(STATUS "Render2D: using local ${dep_name} at '${local_dir}' (no fetch).")
        add_subdirectory("${local_dir}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/${bin_subdir}")
        set(${out_source_dir_var} "${local_dir}")
    else()
        message(STATUS "Render2D: fetching ${dep_name} from ${git_repo} @ ${git_tag}.")
        string(TOLOWER "${dep_name}" _render2d_dep_lc)
        # Download only (SOURCE_SUBDIR points at a path with no CMakeLists.txt, so
        # MakeAvailable populates but does NOT auto add_subdirectory), then add the
        # real source root ourselves -- identical to the local tier, removing any
        # ambiguity in MakeAvailable's auto-add decision.
        FetchContent_Declare(${dep_name}
            GIT_REPOSITORY "${git_repo}"
            GIT_TAG "${git_tag}"
            GIT_SUBMODULES_RECURSE TRUE
            SOURCE_SUBDIR "render2d-no-autoadd")
        FetchContent_MakeAvailable(${dep_name})
        add_subdirectory("${${_render2d_dep_lc}_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/_deps/${bin_subdir}")
        set(${out_source_dir_var} "${${_render2d_dep_lc}_SOURCE_DIR}")
    endif()
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR
            "Render2D: ${dep_name} was resolved but target '${target_name}' is not defined.\n"
            "  fix: point -DRENDER2D_ENGINE_DEPS_ROOT=<root> at a local checkout, or\n"
            "       set the *_GIT_REPOSITORY / *_GIT_TAG cache variables for ${dep_name}.")
    endif()
endmacro()

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

render2d_provide_engine_dependency("MemoryCenterNew" Center.Memory.Headers
    "${RENDER2D_MEMORY_CENTER_SOURCE_DIR}" "MemoryCenterNew"
    "${RENDER2D_MEMORY_CENTER_GIT_REPOSITORY}" "${RENDER2D_MEMORY_CENTER_GIT_TAG}"
    RENDER2D_MEMORY_CENTER_SOURCE_DIR)
render2d_provide_engine_dependency("fast_math" fast_math
    "${RENDER2D_FAST_MATH_SOURCE_DIR}" "fast_math"
    "${RENDER2D_FAST_MATH_GIT_REPOSITORY}" "${RENDER2D_FAST_MATH_GIT_TAG}"
    RENDER2D_FAST_MATH_SOURCE_DIR)
render2d_provide_engine_dependency("ThreadCenter" Center.Thread.Headers
    "${RENDER2D_THREAD_CENTER_SOURCE_DIR}" "ThreadCenter"
    "${RENDER2D_THREAD_CENTER_GIT_REPOSITORY}" "${RENDER2D_THREAD_CENTER_GIT_TAG}"
    RENDER2D_THREAD_CENTER_SOURCE_DIR)

# Vector_New (McVector) is consumed as a header include directory, not a target,
# so it has no "reuse target" tier. Use the local include tree if present, else
# fetch the repo headers-only (SOURCE_SUBDIR points at a path with no
# CMakeLists.txt, so FetchContent populates the tree without add_subdirectory).
if(EXISTS "${RENDER2D_VECTOR_NEW_INCLUDE_DIR}/Center/Memory/Container/Vector/McVector.hpp")
    message(STATUS "Render2D: using local McVector include at '${RENDER2D_VECTOR_NEW_INCLUDE_DIR}' (no fetch).")
else()
    message(STATUS "Render2D: fetching McVector from ${RENDER2D_VECTOR_NEW_GIT_REPOSITORY} @ ${RENDER2D_VECTOR_NEW_GIT_TAG}.")
    FetchContent_Declare(Vector
        GIT_REPOSITORY "${RENDER2D_VECTOR_NEW_GIT_REPOSITORY}"
        GIT_TAG "${RENDER2D_VECTOR_NEW_GIT_TAG}"
        SOURCE_SUBDIR ".render2d-headers-only")
    FetchContent_MakeAvailable(Vector)
    set(RENDER2D_VECTOR_NEW_INCLUDE_DIR "${vector_SOURCE_DIR}/include")
endif()
if(NOT EXISTS "${RENDER2D_VECTOR_NEW_INCLUDE_DIR}/Center/Memory/Container/Vector/McVector.hpp")
    message(FATAL_ERROR
        "Render2D: McVector was resolved but its header was not found under\n"
        "  '${RENDER2D_VECTOR_NEW_INCLUDE_DIR}'.")
endif()

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
