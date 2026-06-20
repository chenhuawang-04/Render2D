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
option(RENDER2D_BUILD_PRESENT_HOST "Build the SDL3 present-host support target (Stage 22): an optional, isolated window+VkSurfaceKHR provider for on-screen presentation. Render2D core stays windowless; a host engine sets this OFF and provides its own surface. See docs/adr/2026-06-16-stage22-sdl3-present-host.md" ON)
option(RENDER2D_ENABLE_AVX2 "Compile consumers with AVX2+FMA (raises the minimum CPU to x86-64-v3 / Haswell 2013+); enables hardware FMA in the fast_math kernels. See docs/adr/2026-06-16-render2d-avx2-fma-build-option.md" OFF)
option(RENDER2D_INSTALL "Generate install/export rules so a downstream project can find_package(Render2D). Default ON in-repo; a host engine that consumes Render2D by source reuse can set it OFF. See docs/adr/2026-06-18-stage25-consumability-packaging.md" ON)

# ----------------------------------------------------------------------------
# Engine dependencies (host-engine source trees, NOT vendored in this repo).
# Resolved in three tiers (reuse target -> local add_subdirectory -> git fetch)
# by the shared cmake/Render2DDependencies.cmake module, which the installed
# Render2DConfig.cmake re-uses verbatim so a find_package(Render2D) consumer
# resolves the same dependencies the same way. include()'d (not add_subdirectory)
# so its add_subdirectory()/FetchContent calls run in this top-level scope.
# ----------------------------------------------------------------------------
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Render2DDependencies.cmake")

if(RENDER2D_REQUIRE_VULKAN)
    find_package(Vulkan REQUIRED)
else()
    find_package(Vulkan QUIET)
endif()

add_library(Render2D INTERFACE)
add_library(Render2D::Render2D ALIAS Render2D)
add_library(render2d_thread_runtime_support INTERFACE)

target_compile_features(Render2D INTERFACE cxx_std_23)

# Stage 24 Track 2: optional AVX2+FMA. With hardware FMA the fast_math sincos
# kernel becomes a single instruction (rotating-transform world build ~7x faster)
# and mat3FromTrsArray takes its 8-wide SIMD path. Raises the minimum CPU to
# x86-64-v3 (Haswell, 2013+), so it is OFF by default to preserve the baseline-ISA
# contract; the clang-ninja-perf preset turns it ON. The math kernels stay correct
# and bit-consistent either way (the scalar path is FMA-conditional). See
# docs/adr/2026-06-16-render2d-avx2-fma-build-option.md. Wrapped in
# $<BUILD_INTERFACE:> so the installed package does not force a specific ISA on
# downstream consumers -- they choose their own; the in-repo build/tests still
# get it when RENDER2D_ENABLE_AVX2 is ON.
if(RENDER2D_ENABLE_AVX2)
    if(MSVC)
        target_compile_options(Render2D INTERFACE $<BUILD_INTERFACE:/arch:AVX2>)
    else()
        target_compile_options(Render2D INTERFACE $<BUILD_INTERFACE:-mavx2> $<BUILD_INTERFACE:-mfma>)
    endif()
endif()
target_include_directories(Render2D INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${RENDER2D_VECTOR_NEW_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)
# The engine-dependency targets + Vulkan are stripped from the INSTALL interface
# (wrapped in $<BUILD_INTERFACE:>) so install(EXPORT) does not require exporting
# targets Render2D does not own. The installed Render2DConfig.cmake re-resolves
# them via cmake/Render2DDependencies.cmake + find_dependency(Vulkan) and
# re-attaches them to the imported targets.
target_link_libraries(Render2D INTERFACE
    $<BUILD_INTERFACE:Center.Memory.Headers>
    $<BUILD_INTERFACE:fast_math>
)

target_link_libraries(render2d_thread_runtime_support INTERFACE
    Render2D::Render2D
    $<BUILD_INTERFACE:Center.Thread.Headers>
)

if(TARGET Vulkan::Vulkan)
    target_link_libraries(Render2D INTERFACE $<BUILD_INTERFACE:Vulkan::Vulkan>)
    target_link_libraries(render2d_thread_runtime_support INTERFACE $<BUILD_INTERFACE:Vulkan::Vulkan>)
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

# Stage 22 SDL3 present-host support. SDL3 builds as a static library behind the
# internal render2d_present_host_support target and is NEVER included from the
# public umbrella Render2D.hpp -- the same isolation pattern as
# render2d_font_runtime_support and render2d_thread_runtime_support. Render2D's
# core stays windowless; this optional target lets a test/dev harness obtain a
# real window + VkSurfaceKHR (via SDL) to feed the existing VulkanSwapchainRuntime,
# which already takes a host-provided surface. A host engine sets
# RENDER2D_BUILD_PRESENT_HOST=OFF at merge and supplies its own surface instead.
# Code that needs it (include/Render2D/Present/*) links this target.
# See docs/adr/2026-06-16-stage22-sdl3-present-host.md.
if(RENDER2D_BUILD_PRESENT_HOST)
    # Static, video-only (window + events + Vulkan surface). Keep SDL_VIDEO and
    # SDL_VULKAN; disable the audio, input-device, camera, power, sensor, dialog,
    # tray, notification subsystems and SDL's own GL/2D/GPU renderers -- Render2D
    # draws through Vulkan directly and the present-host only needs a window and a
    # VkSurfaceKHR. set(... FORCE) before add_subdirectory wins over SDL's option()
    # defaults (same mechanism as the font block above).
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDL_UNINSTALL OFF CACHE BOOL "" FORCE)

    set(SDL_VIDEO ON CACHE BOOL "" FORCE)
    set(SDL_VULKAN ON CACHE BOOL "" FORCE)

    set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
    set(SDL_RENDER OFF CACHE BOOL "" FORCE)
    set(SDL_GPU OFF CACHE BOOL "" FORCE)
    set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
    set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
    set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
    set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
    set(SDL_POWER OFF CACHE BOOL "" FORCE)
    set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
    set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
    set(SDL_TRAY OFF CACHE BOOL "" FORCE)
    set(SDL_NOTIFICATION OFF CACHE BOOL "" FORCE)
    set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
    set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
    # Linux video backends: X11 only (lean + portable). Wayland and KMSDRM would
    # pull in libwayland/libdecor/libdrm/libgbm dev libraries we do not need just
    # to open a window + create a VkSurfaceKHR; X11's dev libs suffice. No-ops on
    # Windows (which uses its own video backend). The host owns its real surface
    # at merge, so this only affects the in-repo present-host demo/tests.
    set(SDL_WAYLAND OFF CACHE BOOL "" FORCE)
    set(SDL_KMSDRM OFF CACHE BOOL "" FORCE)

    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl" "${CMAKE_CURRENT_BINARY_DIR}/_deps/sdl")

    add_library(render2d_present_host_support INTERFACE)
    target_link_libraries(render2d_present_host_support INTERFACE
        Render2D::Render2D
        SDL3::SDL3-static
    )
    if(TARGET Vulkan::Vulkan)
        target_link_libraries(render2d_present_host_support INTERFACE Vulkan::Vulkan)
    endif()

    # Stage 22E: the vendored RenderDoc in-application API header (a single MIT
    # header -- no build, no library, not a submodule; see third_party/renderdoc/
    # README.md). Promoted to a SYSTEM include so it does not trip Render2D's
    # -Werror, and reachable as <renderdoc_app.h> by anything linking this target
    # (e.g. include/Render2D/Present/RenderDocCapture.hpp). When the present-host
    # option is OFF this target does not exist, so the header is never on any
    # include path -- the OFF build is RenderDoc-free as well as SDL-free.
    target_include_directories(render2d_present_host_support SYSTEM INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/renderdoc"
    )

    # Treat SDL's headers as system includes so their warnings do not trip
    # Render2D's -Werror when Present/* includes <SDL3/SDL.h> (same treatment as
    # the font + ThreadCenter headers above). SDL exposes its include dirs via the
    # SDL3::Headers interface target; SDL3-static links it. set_property cannot be
    # used on an ALIAS (SDL3::Headers), so resolve to the real target first.
    foreach(render2d_sdl_target IN ITEMS SDL3-static SDL3::Headers)
        if(TARGET ${render2d_sdl_target})
            get_target_property(render2d_sdl_real ${render2d_sdl_target} ALIASED_TARGET)
            if(NOT render2d_sdl_real)
                set(render2d_sdl_real ${render2d_sdl_target})
            endif()
            get_target_property(render2d_sdl_includes ${render2d_sdl_real} INTERFACE_INCLUDE_DIRECTORIES)
            if(render2d_sdl_includes)
                set_property(TARGET ${render2d_sdl_real} PROPERTY
                    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${render2d_sdl_includes}"
                )
            endif()
        endif()
    endforeach()
endif()

# Stage 25B: install / export so a downstream project can find_package(Render2D).
# Render2D is header-only and its engine dependencies are not vendored, so the
# install tree carries: the public headers, an exported imported target
# (Render2D::Render2D + the thread-runtime support target), and the
# Render2DDependencies.cmake resolver the generated Render2DConfig.cmake re-runs
# to recreate the (stripped) transitive dependencies at the consumer. The font /
# present-host support targets are intentionally NOT exported -- they pull heavy
# optional third-party libraries and present-host is OFF at host merge, so
# consume those by source reuse. See
# docs/adr/2026-06-18-stage25-consumability-packaging.md.
if(RENDER2D_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(RENDER2D_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/Render2D" CACHE STRING
        "Install directory (relative to the prefix) for Render2D's CMake package files.")

    # Everything Render2D installs is tagged COMPONENT Render2D, so
    #   cmake --install <build> --component Render2D --prefix <p>
    # yields a clean Render2D-only tree. (A plain `cmake --install` would also
    # run the dependency subprojects' own install rules -- freetype/harfbuzz/
    # mimalloc/etc. -- which we neither need nor own; the component isolates us.)

    # Public headers. McVector / engine-dep headers are deliberately NOT vendored
    # here -- the installed config re-resolves them.
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/Render2D"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        COMPONENT Render2D)

    set(RENDER2D_EXPORT_TARGETS Render2D)
    if(TARGET render2d_thread_runtime_support)
        list(APPEND RENDER2D_EXPORT_TARGETS render2d_thread_runtime_support)
    endif()
    install(TARGETS ${RENDER2D_EXPORT_TARGETS} EXPORT Render2DTargets COMPONENT Render2D)
    install(EXPORT Render2DTargets
        FILE Render2DTargets.cmake
        NAMESPACE Render2D::
        DESTINATION "${RENDER2D_INSTALL_CMAKEDIR}"
        COMPONENT Render2D)

    if(TARGET Vulkan::Vulkan)
        set(RENDER2D_CONFIG_WITH_VULKAN TRUE)
    else()
        set(RENDER2D_CONFIG_WITH_VULKAN FALSE)
    endif()

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Render2DConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Render2DConfig.cmake"
        INSTALL_DESTINATION "${RENDER2D_INSTALL_CMAKEDIR}")
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/Render2DConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/Render2DConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/Render2DConfigVersion.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Render2DDependencies.cmake"
        DESTINATION "${RENDER2D_INSTALL_CMAKEDIR}"
        COMPONENT Render2D)
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
