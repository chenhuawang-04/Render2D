if(CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo|MinSizeRel")
    if(MSVC)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/UNDEBUG>)
    else()
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-UNDEBUG>)
    endif()
endif()

# Test-only image I/O: an isolated static lib that compiles the vendored stb
# single-headers (third_party/stb) in ONE TU and exposes a clean, stb-free,
# McVector-based API (support/ImageFile.hpp). stb is a SYSTEM include so its
# warning-dirty internals never trip -Werror; this is the only place stb is
# compiled, and it is never linked into the rendering core or the umbrella. The
# asset/scene render test consumes it through support/AssetRegistry.hpp.
add_library(render2d_test_image_io STATIC
    support/ImageFile.cpp
)
target_link_libraries(render2d_test_image_io PUBLIC
    Render2D::Render2D
)
target_include_directories(render2d_test_image_io
    SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/stb
)
# Deliberately NOT render2d_apply_strict_warnings: this TU compiles stb's
# implementation. stb is SYSTEM-included so its warnings are suppressed already; the
# wrapper code itself is small and clean.

add_executable(render2d_compile_smoke
    compile_smoke.cpp
)
target_link_libraries(render2d_compile_smoke PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_compile_smoke)

add_test(NAME render2d.compile_smoke COMMAND render2d_compile_smoke)

# Stage 26 follow-up: GPU capability gate. Always built (links Render2D::Render2D,
# like the vulkan_* tests). It graceful-skips with no device -- but under
# RENDER2D_REQUIRE_GPU it HARD-FAILS, so a green run with that flag set proves a
# device was present and therefore every createVulkanSmokeContext-based test ran
# its real GPU path. _CRT_SECURE_NO_WARNINGS: the env-flag reader uses std::getenv,
# which the MSVC CRT marks deprecated (same treatment as the font tests' fopen).
add_executable(render2d_gpu_presence_gate
    gpu_presence_gate.cpp
)
target_link_libraries(render2d_gpu_presence_gate PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_gpu_presence_gate PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(render2d_gpu_presence_gate PRIVATE _CRT_SECURE_NO_WARNINGS)
render2d_apply_strict_warnings(render2d_gpu_presence_gate)

add_test(NAME render2d.gpu_presence_gate COMMAND render2d_gpu_presence_gate)

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

# Lightweight test-only ECS usability proof: author a scene into MiniEcs and prove
# the gathered span input drives the CPU chain byte-identically to the plain-array
# path, plus entity lifecycle (generational handles + slot reuse). Span-only, so it
# links the public Render2D::Render2D target like the other CPU tests.
add_executable(render2d_mini_ecs_pipeline_test
    mini_ecs_pipeline_test.cpp
)
target_link_libraries(render2d_mini_ecs_pipeline_test PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_mini_ecs_pipeline_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_mini_ecs_pipeline_test)

add_test(NAME render2d.mini_ecs_pipeline COMMAND render2d_mini_ecs_pipeline_test)

# Test-only asset framework end-to-end: load real PNGs through the asset registry,
# author a MiniEcs scene that references them, run the CPU chain (always), and -- on
# a GPU -- upload + sample + render the loaded texture and read it back (skips
# gracefully without a device). Links render2d_test_image_io for the stb-backed
# loader.
add_executable(render2d_asset_scene_render_test
    asset_scene_render_test.cpp
)
target_link_libraries(render2d_asset_scene_render_test PRIVATE
    Render2D::Render2D
    render2d_test_image_io
)
target_include_directories(render2d_asset_scene_render_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_asset_scene_render_test)

add_test(NAME render2d.asset_scene_render COMMAND render2d_asset_scene_render_test)

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

    # Stage 23D (host-engine merge capstone): drive a full frame from host-shaped
    # ECS data (HostLikeEcs) through the span-only CPU systems and the real sprite
    # GPU draw, onto the acquired swapchain image, and present it -- then assert the
    # swapchain readback is byte-identical to the offscreen baseline. The one
    # end-to-end host-data -> on-screen sprite frame proof (closes ProjectMergeTODO
    # #9 for the sprite path). GPU+display+dynamic-rendering+TRANSFER-gated -- skips
    # (returns 0) at every bring-up failure, so it stays green on headless CI.
    add_executable(render2d_host_present_frame_smoke
        host_present_frame_smoke.cpp
    )
    target_link_libraries(render2d_host_present_frame_smoke PRIVATE
        render2d_present_host_support
    )
    target_include_directories(render2d_host_present_frame_smoke PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    render2d_apply_strict_warnings(render2d_host_present_frame_smoke)

    add_test(NAME render2d.host_present_frame COMMAND render2d_host_present_frame_smoke)

    # Window test framework demo: author a MiniEcs + AssetRegistry scene, run it
    # through the span-only chain, and present the resulting sprites to a real SDL
    # window via the WindowTestHarness, asserting the on-screen swapchain capture
    # matches the offscreen baseline. Exercises the new harness (window/instance/
    # device bring-up + swapchain/acquire/capture/present helpers) end to end. Links
    # render2d_test_image_io for the AssetRegistry PNG load (stb). GPU+display+
    # dynamic-rendering+TRANSFER-gated -- skips (returns 0) at every bring-up
    # failure, so it stays green on headless CI.
    add_executable(render2d_window_scene_present_test
        window_scene_present_test.cpp
    )
    target_link_libraries(render2d_window_scene_present_test PRIVATE
        render2d_present_host_support
        render2d_test_image_io
    )
    target_include_directories(render2d_window_scene_present_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    render2d_apply_strict_warnings(render2d_window_scene_present_test)

    add_test(NAME render2d.window_scene_present COMMAND render2d_window_scene_present_test)

    # Present-path frame-timing benchmark: author a scalable grid scene, run it
    # through the span-only chain, and present it in a timed loop via the
    # WindowTestHarness, reporting per-frame latency percentiles + throughput. A
    # measurement tool, not a regression gate (GPU timing is machine-dependent, so
    # it asserts no wall-clock budget -- unlike the deterministic CPU perf gate in
    # bench/). The ctest case runs a handful of frames just to keep the timed path
    # compiled + exercised; real numbers come from a manual run with larger
    # --frames/--sprites. GPU+display+dynamic-rendering+TRANSFER-gated -- skips
    # (returns 0) at every bring-up failure, so it stays green on headless CI. No
    # asset/PNG dependency (it constructs Sprite directly), so unlike the demo it
    # does not link render2d_test_image_io.
    add_executable(render2d_window_scene_bench_test
        window_scene_bench_test.cpp
    )
    target_link_libraries(render2d_window_scene_bench_test PRIVATE
        render2d_present_host_support
    )
    target_include_directories(render2d_window_scene_bench_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    render2d_apply_strict_warnings(render2d_window_scene_bench_test)

    add_test(NAME render2d.window_scene_bench
        COMMAND render2d_window_scene_bench_test --sprites 256 --frames 4 --warmup 1
    )

    # Stage 26 follow-up: present capability gate (the on-screen counterpart of
    # render2d.gpu_presence_gate). Graceful-skips at every bring-up gate, but under
    # RENDER2D_REQUIRE_PRESENT it HARD-FAILS, so a green run with that flag set
    # proves the present path (window -> surface -> 1.3 device -> dynamic rendering
    # -> TRANSFER swapchain) came up and therefore the present_* tests ran for real.
    # _CRT_SECURE_NO_WARNINGS: the env-flag reader uses std::getenv (see above).
    add_executable(render2d_present_capability_gate
        present_capability_gate.cpp
    )
    target_link_libraries(render2d_present_capability_gate PRIVATE
        render2d_present_host_support
    )
    target_include_directories(render2d_present_capability_gate PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_definitions(render2d_present_capability_gate PRIVATE _CRT_SECURE_NO_WARNINGS)
    render2d_apply_strict_warnings(render2d_present_capability_gate)

    add_test(NAME render2d.present_capability_gate COMMAND render2d_present_capability_gate)
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

# Stage 25A: prove the public Render2D::Render2D target is consumable by a
# downstream project via source reuse. The render2d.packaging_consumer test
# drives a fresh configure + build + run of tests/packaging/consumer (a
# standalone CMake project that add_subdirectory()'s Render2D and links
# Render2D::Render2D) through run_consumer.cmake -- exactly how a host engine
# reuses Render2D, so a regression in the public target/include interface fails
# the suite. Reconfigures the engine deps from local source (tier 2), so it
# carries a real build cost; the generous timeout covers a cold dependency build.
add_test(NAME render2d.packaging_consumer
    COMMAND ${CMAKE_COMMAND}
        -D CONSUMER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/packaging/consumer
        -D RENDER2D_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -D WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/packaging_consumer_build
        -D CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -D C_COMPILER=${CMAKE_C_COMPILER}
        -D GENERATOR=${CMAKE_GENERATOR}
        -D ENGINE_DEPS_ROOT=${RENDER2D_ENGINE_DEPS_ROOT}
        -D BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/packaging/run_consumer.cmake
)
set_tests_properties(render2d.packaging_consumer PROPERTIES TIMEOUT 900)

# Stage 25B: prove the INSTALLED Render2D package is consumable via
# find_package(Render2D). Gated on RENDER2D_INSTALL (the install/export rules).
# The test installs Render2D (component Render2D) to a temp prefix, then drives a
# find_package(Render2D) consumer against it -- exercising the config's
# re-resolution of the engine dependencies and re-attachment to the imported
# target. A regression in the exported package fails the suite.
if(RENDER2D_INSTALL)
    add_test(NAME render2d.packaging_installed
        COMMAND ${CMAKE_COMMAND}
            -D CONSUMER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/packaging/installed_consumer
            -D RENDER2D_BUILD_DIR=${CMAKE_BINARY_DIR}
            -D INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/packaging_install_prefix
            -D WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/packaging_installed_build
            -D CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -D C_COMPILER=${CMAKE_C_COMPILER}
            -D GENERATOR=${CMAKE_GENERATOR}
            -D ENGINE_DEPS_ROOT=${RENDER2D_ENGINE_DEPS_ROOT}
            -D BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/packaging/run_installed_consumer.cmake
    )
    set_tests_properties(render2d.packaging_installed PROPERTIES TIMEOUT 900)
endif()
