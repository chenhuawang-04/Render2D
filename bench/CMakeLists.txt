add_executable(render2d_bench_smoke
    bench_smoke.cpp
)
target_link_libraries(render2d_bench_smoke PRIVATE
    Render2D::Render2D
)
render2d_apply_strict_warnings(render2d_bench_smoke)
