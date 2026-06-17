cmake_minimum_required(VERSION 3.28)

# Stage 25A: a standalone downstream consumer of Render2D. This project is NOT
# part of the Render2D build; the render2d.packaging_consumer test drives a fresh
# configure + build + run of it (see ../run_consumer.cmake) against the Render2D
# source tree, proving the public Render2D::Render2D target is consumable by
# source reuse exactly as a host engine reuses it.
project(render2d_consumer LANGUAGES CXX C)

if(NOT DEFINED RENDER2D_SOURCE_DIR OR "${RENDER2D_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "RENDER2D_SOURCE_DIR must point at the Render2D repo root.")
endif()

# Reuse model: pull Render2D in by source. Keep the nested build minimal -- only
# the core INTERFACE library + its header dependencies, no tests/benchmarks/font/
# present-host -- so this proves target consumability, not a full re-test. (A host
# engine sets the same options OFF and supplies its own surface/font runtime.)
set(RENDER2D_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(RENDER2D_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(RENDER2D_BUILD_FONT_RUNTIME OFF CACHE BOOL "" FORCE)
set(RENDER2D_BUILD_PRESENT_HOST OFF CACHE BOOL "" FORCE)
add_subdirectory("${RENDER2D_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/render2d_build")

add_executable(render2d_consumer consumer_main.cpp)
target_link_libraries(render2d_consumer PRIVATE Render2D::Render2D)
