cmake_minimum_required(VERSION 3.28)

# Stage 25B: a standalone consumer of the INSTALLED Render2D package. The
# render2d.packaging_installed test installs Render2D (component Render2D) to a
# temporary prefix, then drives a fresh configure + build + run of this project
# with find_package(Render2D) pointed at that prefix (see
# ../run_installed_consumer.cmake). Proves the exported package is consumable:
# the config re-resolves the engine dependencies and re-attaches them to the
# imported Render2D::Render2D target.
project(render2d_installed_consumer LANGUAGES CXX C)

find_package(Render2D REQUIRED)

# Reuse the same translation unit the source-reuse consumer (25A) uses, so both
# acquisition paths are proven against identical code.
add_executable(render2d_installed_consumer
    "${CMAKE_CURRENT_SOURCE_DIR}/../consumer/consumer_main.cpp")
target_link_libraries(render2d_installed_consumer PRIVATE Render2D::Render2D)
