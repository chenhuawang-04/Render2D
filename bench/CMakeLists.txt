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
render2d_apply_strict_warnings(render2d_null_cpu_bench)

if(RENDER2D_BUILD_TESTS)
    add_test(NAME render2d.null_cpu_bench_smoke
        COMMAND render2d_null_cpu_bench --sprites 1024 --frames 2
    )
endif()
