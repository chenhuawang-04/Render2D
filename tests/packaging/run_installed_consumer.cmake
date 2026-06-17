# Stage 25B driver: install the Render2D package to a temporary prefix, then
# configure + build + run a consumer that find_package(Render2D)'s it from there.
# Invoked by the render2d.packaging_installed CTest via `cmake -P`. Proves the
# exported/installed package (config re-resolves engine deps + re-attaches them)
# is consumable end-to-end.

foreach(required IN ITEMS CONSUMER_SOURCE_DIR RENDER2D_BUILD_DIR INSTALL_PREFIX WORK_DIR CXX_COMPILER C_COMPILER)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required.")
    endif()
endforeach()

file(REMOVE_RECURSE "${INSTALL_PREFIX}")
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

# 1. Install the Render2D package only (component-isolated -> clean tree).
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${RENDER2D_BUILD_DIR}"
        --component Render2D --prefix "${INSTALL_PREFIX}"
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_output)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "Render2D package install failed (${install_result}):\n${install_output}")
endif()

# 2. Configure the consumer with find_package(Render2D) against the install tree.
set(configure_command
    "${CMAKE_COMMAND}"
    -S "${CONSUMER_SOURCE_DIR}"
    -B "${WORK_DIR}"
    -D "CMAKE_CXX_COMPILER=${CXX_COMPILER}"
    -D "CMAKE_C_COMPILER=${C_COMPILER}"
    -D "CMAKE_PREFIX_PATH=${INSTALL_PREFIX}")
if(DEFINED GENERATOR AND NOT "${GENERATOR}" STREQUAL "")
    list(APPEND configure_command -G "${GENERATOR}")
endif()
if(DEFINED ENGINE_DEPS_ROOT AND NOT "${ENGINE_DEPS_ROOT}" STREQUAL "")
    list(APPEND configure_command -D "RENDER2D_ENGINE_DEPS_ROOT=${ENGINE_DEPS_ROOT}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
    list(APPEND configure_command -D "CMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

execute_process(COMMAND ${configure_command}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_output)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "installed-consumer configure failed (${configure_result}):\n${configure_output}")
endif()

# 3. Build + run.
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${WORK_DIR}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_output)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "installed-consumer build failed (${build_result}):\n${build_output}")
endif()

find_program(installed_consumer_exe
    NAMES render2d_installed_consumer
    PATHS "${WORK_DIR}" "${WORK_DIR}/Debug" "${WORK_DIR}/Release" "${WORK_DIR}/RelWithDebInfo"
    NO_DEFAULT_PATH)
if(NOT installed_consumer_exe)
    message(FATAL_ERROR "installed-consumer executable not found under ${WORK_DIR}")
endif()

execute_process(COMMAND "${installed_consumer_exe}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_output)
message(STATUS "installed-consumer output: ${run_output}")
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "installed-consumer run failed (${run_result}):\n${run_output}")
endif()
