add_executable(render2d_compile_smoke
    compile_smoke.cpp
)
target_link_libraries(render2d_compile_smoke PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_compile_smoke)

add_test(NAME render2d.compile_smoke COMMAND render2d_compile_smoke)

add_executable(render2d_cpu_system_pipeline_test
    cpu_system_pipeline_test.cpp
)
target_link_libraries(render2d_cpu_system_pipeline_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_cpu_system_pipeline_test)

add_test(NAME render2d.cpu_system_pipeline COMMAND render2d_cpu_system_pipeline_test)

add_executable(render2d_command_buffer_descriptor_test
    command_buffer_descriptor_test.cpp
)
target_link_libraries(render2d_command_buffer_descriptor_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_command_buffer_descriptor_test)

add_test(NAME render2d.command_buffer_descriptor COMMAND render2d_command_buffer_descriptor_test)

add_executable(render2d_native_components_test
    native_components_test.cpp
)
target_link_libraries(render2d_native_components_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_native_components_test)

add_test(NAME render2d.native_components COMMAND render2d_native_components_test)

add_executable(render2d_temporary_ecs_storage_test
    temporary_ecs_storage_test.cpp
)
target_link_libraries(render2d_temporary_ecs_storage_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_temporary_ecs_storage_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_temporary_ecs_storage_test)

add_test(NAME render2d.temporary_ecs_storage COMMAND render2d_temporary_ecs_storage_test)

add_test(NAME render2d.negative_non_pod_component
    COMMAND ${CMAKE_COMMAND}
        -D CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -D SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/negative_non_pod_component.cpp
        -D OUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/negative_non_pod_component.obj
        -D INCLUDE_DIR=${PROJECT_SOURCE_DIR}/include
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake
)
