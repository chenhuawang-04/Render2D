# Stage 25A driver: configure + build + run the standalone Render2D consumer
# project, asserting each step succeeds. Invoked by the render2d.packaging_consumer
# CTest via `cmake -P`, mirroring expect_compile_failure.cmake's -D/-P convention.
# Proves the public Render2D::Render2D target is consumable by source reuse.

foreach(required IN ITEMS CONSUMER_SOURCE_DIR RENDER2D_SOURCE_DIR WORK_DIR CXX_COMPILER C_COMPILER)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required.")
    endif()
endforeach()

# Start from a clean build tree so the test is hermetic across re-runs.
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(configure_command
    "${CMAKE_COMMAND}"
    -S "${CONSUMER_SOURCE_DIR}"
    -B "${WORK_DIR}"
    -D "CMAKE_CXX_COMPILER=${CXX_COMPILER}"
    -D "CMAKE_C_COMPILER=${C_COMPILER}"
    -D "RENDER2D_SOURCE_DIR=${RENDER2D_SOURCE_DIR}")
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
    message(FATAL_ERROR "consumer configure failed (${configure_result}):\n${configure_output}")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" --build "${WORK_DIR}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_output)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "consumer build failed (${build_result}):\n${build_output}")
endif()

# The built executable location varies by generator/config.
find_program(consumer_exe
    NAMES render2d_consumer
    PATHS "${WORK_DIR}" "${WORK_DIR}/Debug" "${WORK_DIR}/Release" "${WORK_DIR}/RelWithDebInfo"
    NO_DEFAULT_PATH)
if(NOT consumer_exe)
    message(FATAL_ERROR "consumer executable not found under ${WORK_DIR}")
endif()

execute_process(COMMAND "${consumer_exe}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_output)
message(STATUS "consumer output: ${run_output}")
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "consumer run failed (${run_result}):\n${run_output}")
endif()
