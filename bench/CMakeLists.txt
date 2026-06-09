add_executable(render2d_bench_smoke
    bench_smoke.cpp
)
target_link_libraries(render2d_bench_smoke PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_bench_smoke)

add_executable(render2d_null_cpu_bench
    null_cpu_bench.cpp
)
target_link_libraries(render2d_null_cpu_bench PRIVATE
    Render2D::Render2D
)
target_include_directories(render2d_null_cpu_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
render2d_apply_strict_warnings(render2d_null_cpu_bench)

add_executable(render2d_upload_descriptor_compaction_bench
    upload_descriptor_compaction_bench.cpp
)
target_link_libraries(render2d_upload_descriptor_compaction_bench PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_upload_descriptor_compaction_bench)

add_executable(render2d_threaded_cpu_pipeline_bench
    threaded_cpu_pipeline_bench.cpp
)
target_link_libraries(render2d_threaded_cpu_pipeline_bench PRIVATE
    render2d_thread_runtime_support
)
render2d_apply_strict_warnings(render2d_threaded_cpu_pipeline_bench)

if(RENDER2D_BUILD_TESTS)
    add_test(NAME render2d.null_cpu_bench_smoke
        COMMAND render2d_null_cpu_bench --scenario mixed --sprites 1024 --texts 128 --frames 2 --warmup 1
    )
    add_test(NAME render2d.null_cpu_bench_sprite_smoke
        COMMAND render2d_null_cpu_bench --scenario sprite --sprites 1024 --frames 2 --warmup 1 --visibility high
    )
    add_test(NAME render2d.null_cpu_bench_text_smoke
        COMMAND render2d_null_cpu_bench --scenario text --texts 128 --frames 2 --warmup 1 --glyphs-per-text 8 --dirty-text-stride 4
    )
    add_test(NAME render2d.null_cpu_bench_dirty_transform_smoke
        COMMAND render2d_null_cpu_bench --scenario sprite --sprites 1024 --frames 2 --warmup 1 --visibility high --dirty-transform-stride 4
    )
    add_test(NAME render2d.null_cpu_bench_mixed_csv_smoke
        COMMAND render2d_null_cpu_bench --scenario mixed --sprites 1024 --texts 128 --frames 2 --warmup 1 --visibility low --glyphs-per-text 8 --dirty-text-stride 8 --format csv
    )
    add_test(NAME render2d.null_cpu_bench_warmup_zero_smoke
        COMMAND render2d_null_cpu_bench --scenario mixed --sprites 16 --texts 4 --frames 1 --warmup 0 --format csv
    )
    add_test(NAME render2d.upload_descriptor_compaction_bench_smoke
        COMMAND render2d_upload_descriptor_compaction_bench --items 4096 --frames 2 --warmup 1
    )
    add_test(NAME render2d.threaded_cpu_pipeline_bench_smoke
        COMMAND render2d_threaded_cpu_pipeline_bench --sprites 1024 --frames 2 --warmup 1 --workers 2 --min-items-per-task 128
    )
    add_test(NAME render2d.null_cpu_bench_invalid_frames
        COMMAND render2d_null_cpu_bench --scenario mixed --frames nope
    )
    set_tests_properties(render2d.null_cpu_bench_invalid_frames PROPERTIES
        WILL_FAIL TRUE
    )
endif()
