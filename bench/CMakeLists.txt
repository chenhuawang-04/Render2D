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

add_executable(render2d_threaded_text_cpu_pipeline_bench
    threaded_text_cpu_pipeline_bench.cpp
)
target_link_libraries(render2d_threaded_text_cpu_pipeline_bench PRIVATE
    render2d_thread_runtime_support
)
render2d_apply_strict_warnings(render2d_threaded_text_cpu_pipeline_bench)

add_executable(render2d_threaded_draw_sort_bench
    threaded_draw_sort_bench.cpp
)
target_link_libraries(render2d_threaded_draw_sort_bench PRIVATE
    render2d_thread_runtime_support
)
render2d_apply_strict_warnings(render2d_threaded_draw_sort_bench)

# Stage 21B batch tail: parallel deterministic BatchSystem benchmark.
add_executable(render2d_threaded_batch_bench
    threaded_batch_bench.cpp
)
target_link_libraries(render2d_threaded_batch_bench PRIVATE
    render2d_thread_runtime_support
)
render2d_apply_strict_warnings(render2d_threaded_batch_bench)

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
    add_test(NAME render2d.threaded_text_cpu_pipeline_bench_smoke
        COMMAND render2d_threaded_text_cpu_pipeline_bench --texts 1024 --glyphs-per-text 8 --frames 2 --warmup 1 --workers 2 --min-glyphs-per-task 128
    )
    add_test(NAME render2d.threaded_draw_sort_bench_smoke
        COMMAND render2d_threaded_draw_sort_bench --draws 4096 --frames 2 --warmup 1 --workers 2 --min-items-per-task 128
    )
    add_test(NAME render2d.threaded_batch_bench_smoke
        COMMAND render2d_threaded_batch_bench --draws 4096 --frames 2 --warmup 1 --workers 2 --min-items-per-task 128 --run-length 8
    )
    add_test(NAME render2d.null_cpu_bench_invalid_frames
        COMMAND render2d_null_cpu_bench --scenario mixed --frames nope
    )
    set_tests_properties(render2d.null_cpu_bench_invalid_frames PROPERTIES
        WILL_FAIL TRUE
    )

    # ------------------------------------------------------------------------
    # Performance-regression gate (Stage 21). These cases only build under the
    # Perf preset (benchmarks are OFF in Debug), so the wall-clock budgets are
    # always measured on optimized RelWithDebInfo codegen.
    #
    # Each case asserts the DETERMINISTIC, machine-independent work counts the
    # pipeline must produce (visible / total draws / glyph draws / batches) plus
    # a deliberately generous --max-total-avg-ms catastrophe budget. The counts
    # are the precise regression net (a broken culling/batch/sort changes them
    # exactly); the budget only trips on O(n^2)-class slowdowns and is set far
    # above any real baseline so it never flakes on shared CI runners. Expected
    # counts match docs/architecture/BENCHMARK_BASELINE.md.
    # ------------------------------------------------------------------------
    set(RENDER2D_PERF_GATE_MAX_TOTAL_AVG_MS "25" CACHE STRING
        "Per-frame summed stage-time budget (ms) for the perf-regression gate.")

    add_test(NAME render2d.perf_gate_sprite_high
        COMMAND render2d_null_cpu_bench --scenario sprite --sprites 10000 --visibility high
            --frames 4 --warmup 1
            --expect-visible 10000 --expect-total-draws 10000 --expect-batches 79
            --max-total-avg-ms ${RENDER2D_PERF_GATE_MAX_TOTAL_AVG_MS}
    )
    add_test(NAME render2d.perf_gate_sprite_low
        COMMAND render2d_null_cpu_bench --scenario sprite --sprites 10000 --visibility low
            --frames 4 --warmup 1
            --expect-visible 1250 --expect-total-draws 1250 --expect-batches 79
            --max-total-avg-ms ${RENDER2D_PERF_GATE_MAX_TOTAL_AVG_MS}
    )
    add_test(NAME render2d.perf_gate_sprite_sorted
        COMMAND render2d_null_cpu_bench --scenario sprite --sprites 10000 --visibility high --enable-sort
            --frames 4 --warmup 1
            --expect-visible 10000 --expect-total-draws 10000 --expect-batches 8
            --max-total-avg-ms ${RENDER2D_PERF_GATE_MAX_TOTAL_AVG_MS}
    )
    add_test(NAME render2d.perf_gate_text
        COMMAND render2d_null_cpu_bench --scenario text --texts 2048 --glyphs-per-text 8 --dirty-text-stride 0
            --frames 4 --warmup 1
            --expect-total-draws 2048 --expect-glyph-draws 2048 --expect-batches 2048
            --max-total-avg-ms ${RENDER2D_PERF_GATE_MAX_TOTAL_AVG_MS}
    )
    add_test(NAME render2d.perf_gate_text_sorted
        COMMAND render2d_null_cpu_bench --scenario text --texts 2048 --glyphs-per-text 8 --dirty-text-stride 0 --enable-sort
            --frames 4 --warmup 1
            --expect-total-draws 2048 --expect-glyph-draws 2048 --expect-batches 16
            --max-total-avg-ms ${RENDER2D_PERF_GATE_MAX_TOTAL_AVG_MS}
    )
    add_test(NAME render2d.perf_gate_mixed
        COMMAND render2d_null_cpu_bench --scenario mixed --sprites 10000 --texts 2048
            --glyphs-per-text 8 --dirty-text-stride 8 --frames 4 --warmup 1
            --expect-visible 10000 --expect-total-draws 12048 --expect-glyph-draws 2048 --expect-batches 2127
            --max-total-avg-ms ${RENDER2D_PERF_GATE_MAX_TOTAL_AVG_MS}
    )
    add_test(NAME render2d.perf_gate_mixed_sorted
        COMMAND render2d_null_cpu_bench --scenario mixed --sprites 10000 --texts 2048
            --glyphs-per-text 8 --dirty-text-stride 8 --enable-sort --frames 4 --warmup 1
            --expect-visible 10000 --expect-total-draws 12048 --expect-glyph-draws 2048 --expect-batches 24
            --max-total-avg-ms ${RENDER2D_PERF_GATE_MAX_TOTAL_AVG_MS}
    )
endif()
