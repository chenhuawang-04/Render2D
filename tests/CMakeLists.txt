if(CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo|MinSizeRel")
    if(MSVC)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/UNDEBUG>)
    else()
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-UNDEBUG>)
    endif()
endif()

add_executable(render2d_compile_smoke
    compile_smoke.cpp
)
target_link_libraries(render2d_compile_smoke PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_compile_smoke)

add_test(NAME render2d.compile_smoke COMMAND render2d_compile_smoke)

add_executable(render2d_test_harness_test
    test_harness_test.cpp
)
target_link_libraries(render2d_test_harness_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_test_harness_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_test_harness_test)

add_test(NAME render2d.test_harness COMMAND render2d_test_harness_test)

add_executable(render2d_thread_center_dependency_test
    thread_center_dependency_test.cpp
)
target_link_libraries(render2d_thread_center_dependency_test PRIVATE
    render2d_thread_runtime_support
)
target_include_directories(render2d_thread_center_dependency_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_thread_center_dependency_test)

add_test(NAME render2d.thread_center_dependency COMMAND render2d_thread_center_dependency_test)

add_executable(render2d_threaded_cpu_pipeline_test
    threaded_cpu_pipeline_test.cpp
)
target_link_libraries(render2d_threaded_cpu_pipeline_test PRIVATE
    render2d_thread_runtime_support
)
target_include_directories(render2d_threaded_cpu_pipeline_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_threaded_cpu_pipeline_test)

add_test(NAME render2d.threaded_cpu_pipeline COMMAND render2d_threaded_cpu_pipeline_test)

add_executable(render2d_threaded_text_cpu_pipeline_test
    threaded_text_cpu_pipeline_test.cpp
)
target_link_libraries(render2d_threaded_text_cpu_pipeline_test PRIVATE
    render2d_thread_runtime_support
)
target_include_directories(render2d_threaded_text_cpu_pipeline_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_threaded_text_cpu_pipeline_test)

add_test(NAME render2d.threaded_text_cpu_pipeline COMMAND render2d_threaded_text_cpu_pipeline_test)

add_executable(render2d_threaded_draw_sort_test
    threaded_draw_sort_test.cpp
)
target_link_libraries(render2d_threaded_draw_sort_test PRIVATE
    render2d_thread_runtime_support
)
target_include_directories(render2d_threaded_draw_sort_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_threaded_draw_sort_test)

add_test(NAME render2d.threaded_draw_sort COMMAND render2d_threaded_draw_sort_test)

add_executable(render2d_threaded_batch_system_test
    threaded_batch_system_test.cpp
)
target_link_libraries(render2d_threaded_batch_system_test PRIVATE
    render2d_thread_runtime_support
)
target_include_directories(render2d_threaded_batch_system_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_threaded_batch_system_test)

add_test(NAME render2d.threaded_batch_system COMMAND render2d_threaded_batch_system_test)

add_executable(render2d_cpu_system_pipeline_test
    cpu_system_pipeline_test.cpp
)
target_link_libraries(render2d_cpu_system_pipeline_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_cpu_system_pipeline_test)

add_test(NAME render2d.cpu_system_pipeline COMMAND render2d_cpu_system_pipeline_test)

add_executable(render2d_host_like_ecs_adapter_test
    host_like_ecs_adapter_test.cpp
)
target_link_libraries(render2d_host_like_ecs_adapter_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_host_like_ecs_adapter_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_host_like_ecs_adapter_test)

add_test(NAME render2d.host_like_ecs_adapter COMMAND render2d_host_like_ecs_adapter_test)

add_executable(render2d_draw_sort_system_test
    draw_sort_system_test.cpp
)
target_link_libraries(render2d_draw_sort_system_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_draw_sort_system_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_draw_sort_system_test)

add_test(NAME render2d.draw_sort_system COMMAND render2d_draw_sort_system_test)

add_executable(render2d_bindless_batch_system_test
    bindless_batch_system_test.cpp
)
target_link_libraries(render2d_bindless_batch_system_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_bindless_batch_system_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_bindless_batch_system_test)

add_test(NAME render2d.bindless_batch_system COMMAND render2d_bindless_batch_system_test)

add_executable(render2d_sprite_material_graph_test
    sprite_material_graph_test.cpp
)
target_link_libraries(render2d_sprite_material_graph_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_sprite_material_graph_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_sprite_material_graph_test)

add_test(NAME render2d.sprite_material_graph COMMAND render2d_sprite_material_graph_test)

add_executable(render2d_sprite_instance_system_test
    sprite_instance_system_test.cpp
)
target_link_libraries(render2d_sprite_instance_system_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_sprite_instance_system_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_sprite_instance_system_test)

add_test(NAME render2d.sprite_instance_system COMMAND render2d_sprite_instance_system_test)

add_executable(render2d_sprite_instance_upload_system_test
    sprite_instance_upload_system_test.cpp
)
target_link_libraries(render2d_sprite_instance_upload_system_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_sprite_instance_upload_system_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_sprite_instance_upload_system_test)

add_test(NAME render2d.sprite_instance_upload_system COMMAND render2d_sprite_instance_upload_system_test)

add_executable(render2d_sprite_pipeline_contract_test
    sprite_pipeline_contract_test.cpp
)
target_link_libraries(render2d_sprite_pipeline_contract_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_sprite_pipeline_contract_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_sprite_pipeline_contract_test)

add_test(NAME render2d.sprite_pipeline_contract COMMAND render2d_sprite_pipeline_contract_test)

add_executable(render2d_upload_descriptor_compaction_test
    upload_descriptor_compaction_test.cpp
)
target_link_libraries(render2d_upload_descriptor_compaction_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_upload_descriptor_compaction_test)

add_test(NAME render2d.upload_descriptor_compaction COMMAND render2d_upload_descriptor_compaction_test)

add_executable(render2d_transform_dirty_system_test
    transform_dirty_system_test.cpp
)
target_link_libraries(render2d_transform_dirty_system_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_transform_dirty_system_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_transform_dirty_system_test)

add_test(NAME render2d.transform_dirty_system COMMAND render2d_transform_dirty_system_test)

add_executable(render2d_bounds_system_test
    bounds_system_test.cpp
)
target_link_libraries(render2d_bounds_system_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_bounds_system_test)

add_test(NAME render2d.bounds_system COMMAND render2d_bounds_system_test)

add_executable(render2d_spatial_cull_system_test
    spatial_cull_system_test.cpp
)
target_link_libraries(render2d_spatial_cull_system_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_spatial_cull_system_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_spatial_cull_system_test)

add_test(NAME render2d.spatial_cull_system COMMAND render2d_spatial_cull_system_test)

add_executable(render2d_batched_transform_system_test
    batched_transform_system_test.cpp
)
target_link_libraries(render2d_batched_transform_system_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_batched_transform_system_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_batched_transform_system_test)

add_test(NAME render2d.batched_transform_system COMMAND render2d_batched_transform_system_test)


add_executable(render2d_command_buffer_descriptor_test
    command_buffer_descriptor_test.cpp
)
target_link_libraries(render2d_command_buffer_descriptor_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_command_buffer_descriptor_test)

add_test(NAME render2d.command_buffer_descriptor COMMAND render2d_command_buffer_descriptor_test)

add_executable(render2d_text_glyph_components_test
    text_glyph_components_test.cpp
)
target_link_libraries(render2d_text_glyph_components_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_text_glyph_components_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_text_glyph_components_test)

add_test(NAME render2d.text_glyph_components COMMAND render2d_text_glyph_components_test)

add_executable(render2d_text_shaping_system_test
    text_shaping_system_test.cpp
)
target_link_libraries(render2d_text_shaping_system_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_text_shaping_system_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_text_shaping_system_test)

add_test(NAME render2d.text_shaping_system COMMAND render2d_text_shaping_system_test)

if(RENDER2D_BUILD_FONT_RUNTIME AND TARGET render2d_font_runtime_support)
    add_executable(render2d_bidi_itemize_runtime_test
        bidi_itemize_runtime_test.cpp
    )
    target_link_libraries(render2d_bidi_itemize_runtime_test PRIVATE
        render2d_font_runtime_support
    )
    target_include_directories(render2d_bidi_itemize_runtime_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    render2d_apply_strict_warnings(render2d_bidi_itemize_runtime_test)

    add_test(NAME render2d.bidi_itemize_runtime COMMAND render2d_bidi_itemize_runtime_test)

    add_executable(render2d_font_shape_runtime_test
        font_shape_runtime_test.cpp
    )
    target_link_libraries(render2d_font_shape_runtime_test PRIVATE
        render2d_font_runtime_support
    )
    target_include_directories(render2d_font_shape_runtime_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_definitions(render2d_font_shape_runtime_test PRIVATE _CRT_SECURE_NO_WARNINGS)
    render2d_apply_strict_warnings(render2d_font_shape_runtime_test)

    add_test(NAME render2d.font_shape_runtime COMMAND render2d_font_shape_runtime_test)

    add_executable(render2d_vulkan_glyph_text_render_test
        vulkan_glyph_text_render_test.cpp
    )
    target_link_libraries(render2d_vulkan_glyph_text_render_test PRIVATE
        render2d_font_runtime_support
    )
    target_include_directories(render2d_vulkan_glyph_text_render_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_definitions(render2d_vulkan_glyph_text_render_test PRIVATE _CRT_SECURE_NO_WARNINGS)
    render2d_apply_strict_warnings(render2d_vulkan_glyph_text_render_test)

    add_test(NAME render2d.vulkan_glyph_text_render COMMAND render2d_vulkan_glyph_text_render_test)
endif()

# Stage 22A: SDL3 present-host build/link smoke. Gated on the present-host option
# + target so a RENDER2D_BUILD_PRESENT_HOST=OFF build (e.g. host-engine merge)
# simply omits it. Links the internal render2d_present_host_support target.
if(RENDER2D_BUILD_PRESENT_HOST AND TARGET render2d_present_host_support)
    add_executable(render2d_present_host_smoke
        present_host_smoke.cpp
    )
    target_link_libraries(render2d_present_host_smoke PRIVATE
        render2d_present_host_support
    )
    target_include_directories(render2d_present_host_smoke PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    render2d_apply_strict_warnings(render2d_present_host_smoke)

    add_test(NAME render2d.present_host_smoke COMMAND render2d_present_host_smoke)

    # Stage 22B: drive the real surface->swapchain path through a live
    # SDL3-provided VkSurfaceKHR. GPU+display-gated -- it skips (returns 0) at
    # every bring-up failure, so it stays green on headless CI.
    add_executable(render2d_present_host_window_smoke
        present_host_window_smoke.cpp
    )
    target_link_libraries(render2d_present_host_window_smoke PRIVATE
        render2d_present_host_support
    )
    target_include_directories(render2d_present_host_window_smoke PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    render2d_apply_strict_warnings(render2d_present_host_window_smoke)

    add_test(NAME render2d.present_host_window_smoke COMMAND render2d_present_host_window_smoke)

    # Stage 22C: present real frames (clear -> present) through the unchanged
    # Vulkan runtimes on the live SDL surface, plus the resize/recreate path.
    # GPU+display-gated -- skips (returns 0) at every bring-up failure, so it
    # stays green on headless CI.
    add_executable(render2d_present_loop_smoke
        present_loop_smoke.cpp
    )
    target_link_libraries(render2d_present_loop_smoke PRIVATE
        render2d_present_host_support
    )
    target_include_directories(render2d_present_loop_smoke PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    render2d_apply_strict_warnings(render2d_present_loop_smoke)

    add_test(NAME render2d.present_loop_smoke COMMAND render2d_present_loop_smoke)

    # Stage 22D: render a real scene offscreen, copy it onto the acquired
    # swapchain image, present, and assert the swapchain readback is byte-
    # identical to the offscreen baseline -- a visible-capture regression guard.
    # GPU+display-gated (also needs dynamic rendering + TRANSFER swapchain
    # usage) -- skips (returns 0) at every bring-up failure, so it stays green
    # on headless CI.
    add_executable(render2d_present_visible_capture_smoke
        present_visible_capture_smoke.cpp
    )
    target_link_libraries(render2d_present_visible_capture_smoke PRIVATE
        render2d_present_host_support
    )
    target_include_directories(render2d_present_visible_capture_smoke PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    render2d_apply_strict_warnings(render2d_present_visible_capture_smoke)

    add_test(NAME render2d.present_visible_capture COMMAND render2d_present_visible_capture_smoke)

    # Stage 22E: render a real gradient frame to the live swapchain and wrap the
    # submit+present in a programmatic RenderDoc capture (a repeatable .rdc
    # artifact). RenderDocCapture only attaches to an already-injected RenderDoc,
    # so a normal ctest run is a no-op and just proves the present frame; the
    # actual capture is the manual under-RenderDoc check. GPU+display+dynamic-
    # rendering-gated -- skips (returns 0) at every bring-up failure, staying
    # green on headless CI.
    add_executable(render2d_present_renderdoc_capture_smoke
        present_renderdoc_capture_smoke.cpp
    )
    target_link_libraries(render2d_present_renderdoc_capture_smoke PRIVATE
        render2d_present_host_support
    )
    target_include_directories(render2d_present_renderdoc_capture_smoke PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    render2d_apply_strict_warnings(render2d_present_renderdoc_capture_smoke)

    add_test(NAME render2d.present_renderdoc_capture COMMAND render2d_present_renderdoc_capture_smoke)
endif()

add_executable(render2d_text_glyph_system_test
    text_glyph_system_test.cpp
)
target_link_libraries(render2d_text_glyph_system_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_text_glyph_system_test)

add_test(NAME render2d.text_glyph_system COMMAND render2d_text_glyph_system_test)

add_executable(render2d_text_stage9_pipeline_test
    text_stage9_pipeline_test.cpp
)
target_link_libraries(render2d_text_stage9_pipeline_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_text_stage9_pipeline_test)

add_test(NAME render2d.text_stage9_pipeline COMMAND render2d_text_stage9_pipeline_test)

add_executable(render2d_native_components_test
    native_components_test.cpp
)
target_link_libraries(render2d_native_components_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_native_components_test)

add_test(NAME render2d.native_components COMMAND render2d_native_components_test)

add_executable(render2d_native_runtime_contract_test
    native_runtime_contract_test.cpp
)
target_link_libraries(render2d_native_runtime_contract_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_native_runtime_contract_test)

add_test(NAME render2d.native_runtime_contract COMMAND render2d_native_runtime_contract_test)

add_executable(render2d_native_deferred_destroy_runtime_test
    native_deferred_destroy_runtime_test.cpp
)
target_link_libraries(render2d_native_deferred_destroy_runtime_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_native_deferred_destroy_runtime_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_native_deferred_destroy_runtime_test)

add_test(NAME render2d.native_deferred_destroy_runtime COMMAND render2d_native_deferred_destroy_runtime_test)

add_executable(render2d_native_resource_runtime_test
    native_resource_runtime_test.cpp
)
target_link_libraries(render2d_native_resource_runtime_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_native_resource_runtime_test)

add_test(NAME render2d.native_resource_runtime COMMAND render2d_native_resource_runtime_test)

add_executable(render2d_native_runtime_skeleton_test
    native_runtime_skeleton_test.cpp
)
target_link_libraries(render2d_native_runtime_skeleton_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_native_runtime_skeleton_test)

add_test(NAME render2d.native_runtime_skeleton COMMAND render2d_native_runtime_skeleton_test)

add_executable(render2d_native_command_runtime_test
    native_command_runtime_test.cpp
)
target_link_libraries(render2d_native_command_runtime_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_native_command_runtime_test)

add_test(NAME render2d.native_command_runtime COMMAND render2d_native_command_runtime_test)

add_executable(render2d_encode_submit_system_test
    encode_submit_system_test.cpp
)
target_link_libraries(render2d_encode_submit_system_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_encode_submit_system_test)

add_test(NAME render2d.encode_submit_system COMMAND render2d_encode_submit_system_test)

add_executable(render2d_present_command_system_test
    present_command_system_test.cpp
)
target_link_libraries(render2d_present_command_system_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_present_command_system_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_present_command_system_test)

add_test(NAME render2d.present_command_system COMMAND render2d_present_command_system_test)

add_executable(render2d_vulkan_command_runtime_test
    vulkan_command_runtime_test.cpp
)
target_link_libraries(render2d_vulkan_command_runtime_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_vulkan_command_runtime_test)

add_test(NAME render2d.vulkan_command_runtime COMMAND render2d_vulkan_command_runtime_test)

add_executable(render2d_vulkan_thread_command_runtime_test
    vulkan_thread_command_runtime_test.cpp
)
target_link_libraries(render2d_vulkan_thread_command_runtime_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_vulkan_thread_command_runtime_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_vulkan_thread_command_runtime_test)

add_test(NAME render2d.vulkan_thread_command_runtime COMMAND render2d_vulkan_thread_command_runtime_test)

add_executable(render2d_vulkan_sync_runtime_test
    vulkan_sync_runtime_test.cpp
)
target_link_libraries(render2d_vulkan_sync_runtime_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_vulkan_sync_runtime_test)

add_test(NAME render2d.vulkan_sync_runtime COMMAND render2d_vulkan_sync_runtime_test)

add_executable(render2d_vulkan_submit_runtime_test
    vulkan_submit_runtime_test.cpp
)
target_link_libraries(render2d_vulkan_submit_runtime_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_vulkan_submit_runtime_test)

add_test(NAME render2d.vulkan_submit_runtime COMMAND render2d_vulkan_submit_runtime_test)

add_executable(render2d_vulkan_swapchain_runtime_test
    vulkan_swapchain_runtime_test.cpp
)
target_link_libraries(render2d_vulkan_swapchain_runtime_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_vulkan_swapchain_runtime_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_vulkan_swapchain_runtime_test)

add_test(NAME render2d.vulkan_swapchain_runtime COMMAND render2d_vulkan_swapchain_runtime_test)

add_executable(render2d_vulkan_resource_runtime_test
    vulkan_resource_runtime_test.cpp
)
target_link_libraries(render2d_vulkan_resource_runtime_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_vulkan_resource_runtime_test)

add_test(NAME render2d.vulkan_resource_runtime COMMAND render2d_vulkan_resource_runtime_test)

add_executable(render2d_vulkan_validation_layer_smoke_test
    vulkan_validation_layer_smoke_test.cpp
)
target_link_libraries(render2d_vulkan_validation_layer_smoke_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_vulkan_validation_layer_smoke_test)

add_test(NAME render2d.vulkan_validation_layer_smoke COMMAND render2d_vulkan_validation_layer_smoke_test)

add_executable(render2d_vulkan_texture_atlas_runtime_test
    vulkan_texture_atlas_runtime_test.cpp
)
target_link_libraries(render2d_vulkan_texture_atlas_runtime_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_vulkan_texture_atlas_runtime_test)

add_test(NAME render2d.vulkan_texture_atlas_runtime COMMAND render2d_vulkan_texture_atlas_runtime_test)

add_executable(render2d_vulkan_descriptor_runtime_test
    vulkan_descriptor_runtime_test.cpp
)
target_link_libraries(render2d_vulkan_descriptor_runtime_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_vulkan_descriptor_runtime_test)

add_test(NAME render2d.vulkan_descriptor_runtime COMMAND render2d_vulkan_descriptor_runtime_test)

add_executable(render2d_vulkan_bindless_capability_test
    vulkan_bindless_capability_test.cpp
)
target_link_libraries(render2d_vulkan_bindless_capability_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_vulkan_bindless_capability_test)

add_test(NAME render2d.vulkan_bindless_capability COMMAND render2d_vulkan_bindless_capability_test)

add_executable(render2d_vulkan_bindless_texture_table_test
    vulkan_bindless_texture_table_test.cpp
)
target_link_libraries(render2d_vulkan_bindless_texture_table_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_vulkan_bindless_texture_table_test)

add_test(NAME render2d.vulkan_bindless_texture_table COMMAND render2d_vulkan_bindless_texture_table_test)

add_executable(render2d_vulkan_bindless_sprite_render_test
    vulkan_bindless_sprite_render_test.cpp
)
target_link_libraries(render2d_vulkan_bindless_sprite_render_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_vulkan_bindless_sprite_render_test)

add_test(NAME render2d.vulkan_bindless_sprite_render COMMAND render2d_vulkan_bindless_sprite_render_test)

add_executable(render2d_vulkan_pipeline_runtime_test
    vulkan_pipeline_runtime_test.cpp
)
target_link_libraries(render2d_vulkan_pipeline_runtime_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_vulkan_pipeline_runtime_test)

add_test(NAME render2d.vulkan_pipeline_runtime COMMAND render2d_vulkan_pipeline_runtime_test)

add_executable(render2d_vulkan_present_runtime_test
    vulkan_present_runtime_test.cpp
)
target_link_libraries(render2d_vulkan_present_runtime_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_vulkan_present_runtime_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_vulkan_present_runtime_test)

add_test(NAME render2d.vulkan_present_runtime COMMAND render2d_vulkan_present_runtime_test)

add_executable(render2d_vulkan_upload_ring_runtime_test
    vulkan_upload_ring_runtime_test.cpp
)
target_link_libraries(render2d_vulkan_upload_ring_runtime_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_vulkan_upload_ring_runtime_test)

add_test(NAME render2d.vulkan_upload_ring_runtime COMMAND render2d_vulkan_upload_ring_runtime_test)

add_executable(render2d_vulkan_sprite_instance_upload_runtime_test
    vulkan_sprite_instance_upload_runtime_test.cpp
)
target_link_libraries(render2d_vulkan_sprite_instance_upload_runtime_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_vulkan_sprite_instance_upload_runtime_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_vulkan_sprite_instance_upload_runtime_test)

add_test(NAME render2d.vulkan_sprite_instance_upload_runtime COMMAND render2d_vulkan_sprite_instance_upload_runtime_test)

add_executable(render2d_vulkan_sprite_pipeline_runtime_test
    vulkan_sprite_pipeline_runtime_test.cpp
)
target_link_libraries(render2d_vulkan_sprite_pipeline_runtime_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_vulkan_sprite_pipeline_runtime_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_vulkan_sprite_pipeline_runtime_test)

add_test(NAME render2d.vulkan_sprite_pipeline_runtime COMMAND render2d_vulkan_sprite_pipeline_runtime_test)

add_executable(render2d_vulkan_sprite_render_encoder_test
    vulkan_sprite_render_encoder_test.cpp
)
target_link_libraries(render2d_vulkan_sprite_render_encoder_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_vulkan_sprite_render_encoder_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_vulkan_sprite_render_encoder_test)

add_test(NAME render2d.vulkan_sprite_render_encoder COMMAND render2d_vulkan_sprite_render_encoder_test)

add_executable(render2d_vulkan_textured_sprite_render_test
    vulkan_textured_sprite_render_test.cpp
)
target_link_libraries(render2d_vulkan_textured_sprite_render_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_vulkan_textured_sprite_render_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_vulkan_textured_sprite_render_test)

add_test(NAME render2d.vulkan_textured_sprite_render COMMAND render2d_vulkan_textured_sprite_render_test)

add_executable(render2d_vulkan_dynamic_render_encoder_test
    vulkan_dynamic_render_encoder_test.cpp
)
target_link_libraries(render2d_vulkan_dynamic_render_encoder_test PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_vulkan_dynamic_render_encoder_test)

add_test(NAME render2d.vulkan_dynamic_render_encoder COMMAND render2d_vulkan_dynamic_render_encoder_test)

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
        -D EXTRA_INCLUDE_DIRS=${Vulkan_INCLUDE_DIR}
        -D VECTOR_NEW_INCLUDE_DIR=${RENDER2D_VECTOR_NEW_INCLUDE_DIR}
        -D MEMORY_CENTER_INCLUDE_DIR=${RENDER2D_MEMORY_CENTER_SOURCE_DIR}/include
        -D MIMALLOC_INCLUDE_DIR=${RENDER2D_MEMORY_CENTER_SOURCE_DIR}/third_party/mimalloc/include
        -D FAST_MATH_INCLUDE_DIR=${RENDER2D_FAST_MATH_SOURCE_DIR}/include
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake
)
