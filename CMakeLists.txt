cmake_minimum_required(VERSION 3.25)

project(Render2D
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RENDER2D_BUILD_TESTS "Build Render2D tests" ON)
option(RENDER2D_BUILD_BENCHMARKS "Build Render2D benchmark smoke targets" OFF)
option(RENDER2D_REQUIRE_VULKAN "Require the Vulkan SDK at configure time" ON)

set(RENDER2D_MEMORY_CENTER_SOURCE_DIR "E:/Project/MelosyneTest/MemoryCenterNew" CACHE PATH
    "Path to MemoryCenterNew source tree.")
set(RENDER2D_FAST_MATH_SOURCE_DIR "E:/Project/MelosyneTest/Math/fast_math" CACHE PATH
    "Path to fast_math source tree.")
set(RENDER2D_VECTOR_NEW_INCLUDE_DIR "E:/Project/MelosyneTest/Vector_New/include" CACHE PATH
    "Path to Vector_New include tree providing Center/Memory/Container/Vector/McVector.hpp.")

if(NOT EXISTS "${RENDER2D_MEMORY_CENTER_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "MemoryCenterNew was not found at RENDER2D_MEMORY_CENTER_SOURCE_DIR='${RENDER2D_MEMORY_CENTER_SOURCE_DIR}'.")
endif()
if(NOT EXISTS "${RENDER2D_FAST_MATH_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "fast_math was not found at RENDER2D_FAST_MATH_SOURCE_DIR='${RENDER2D_FAST_MATH_SOURCE_DIR}'.")
endif()
if(NOT EXISTS "${RENDER2D_VECTOR_NEW_INCLUDE_DIR}/Center/Memory/Container/Vector/McVector.hpp")
    message(FATAL_ERROR "McVector was not found at RENDER2D_VECTOR_NEW_INCLUDE_DIR='${RENDER2D_VECTOR_NEW_INCLUDE_DIR}'.")
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

add_subdirectory("${RENDER2D_MEMORY_CENTER_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/MemoryCenterNew")
add_subdirectory("${RENDER2D_FAST_MATH_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/fast_math")

if(RENDER2D_REQUIRE_VULKAN)
    find_package(Vulkan REQUIRED)
else()
    find_package(Vulkan QUIET)
endif()

add_library(Render2D INTERFACE)
add_library(Render2D::Render2D ALIAS Render2D)

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

if(TARGET Vulkan::Vulkan)
    target_link_libraries(Render2D INTERFACE Vulkan::Vulkan)
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
