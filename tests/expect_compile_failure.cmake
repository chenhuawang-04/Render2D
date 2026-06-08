if(NOT DEFINED CXX_COMPILER)
    message(FATAL_ERROR "CXX_COMPILER is required.")
endif()
if(NOT DEFINED SOURCE_FILE)
    message(FATAL_ERROR "SOURCE_FILE is required.")
endif()
if(NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "OUTPUT_FILE is required.")
endif()
if(NOT DEFINED INCLUDE_DIR)
    message(FATAL_ERROR "INCLUDE_DIR is required.")
endif()

set(extra_include_args)
if(DEFINED EXTRA_INCLUDE_DIRS)
    foreach(extra_include_dir IN LISTS EXTRA_INCLUDE_DIRS)
        if(NOT extra_include_dir STREQUAL "")
            list(APPEND extra_include_args -I "${extra_include_dir}")
        endif()
    endforeach()
endif()

execute_process(
    COMMAND "${CXX_COMPILER}" -std=c++23 -I "${INCLUDE_DIR}" ${extra_include_args} -c "${SOURCE_FILE}" -o "${OUTPUT_FILE}"
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)

if(compile_result EQUAL 0)
    message(FATAL_ERROR "Expected compile failure, but compilation succeeded: ${SOURCE_FILE}")
endif()

set(compile_output "${compile_stdout}\n${compile_stderr}")
if(NOT compile_output MATCHES "StrictPodComponent|Unsupported Render2D component")
    message(FATAL_ERROR "Compile failed for an unexpected reason:\n${compile_output}")
endif()
