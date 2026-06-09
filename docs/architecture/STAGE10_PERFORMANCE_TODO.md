# Stage 10 Performance TODO

This file is the execution checklist for fully completing Stage 10. It keeps optimization order explicit so each change has a benchmark before/after and does not break ECS/Strict POD boundaries.

## Completed

- [x] 10A: Test/benchmark framework exists.
- [x] 10B: Standard Null CPU benchmark runner and baseline document exist.
- [x] 10C: Render math migrated to fast_math POD aliases and BoundsSystem hot path was reduced.
- [x] 10D: Benchmark/profile harness now includes Perf preset, dirty transform scenarios, large/huge local suites, and profile runner metadata.
- [x] 10E: Single-thread Transform/Bounds dirty-index path and zero-rotation Transform fast path are implemented and benchmarked.
- [x] 10F: Packed draw sort keys, explicit radix sort path, and BatchSystem packed-key comparison are implemented and benchmarked.
- [x] 10G: ThreadCenter header dependency is integrated as runtime/system infrastructure only through an internal support target and smoke test.

## Remaining Route

- [ ] 10H: ThreadCenter-backed multi-thread CPU pipeline with deterministic merge.
- [ ] 10I: Upload command coalescing and descriptor table compaction.
- [ ] 10J: Per-thread Vulkan command pools and command recording ownership.
- [ ] 10K: Final Stage 10 quality gate and documentation closeout.

## Non-negotiable Constraints

- Components remain Strict POD.
- Host ECS owns component streams; Render2D does not introduce production ECS storage.
- Render2D-owned dynamic arrays use `McVector` / MemoryCenter.
- Math remains fast_math-only.
- ThreadCenter types must not enter ECS components.
- No direct Vulkan memory allocation/mapping API calls.
- Every optimization records benchmark before/after.

## ThreadCenter Integration Boundary

ThreadCenter source root is expected at:

```text
E:/Project/MelosyneTest/ThreadCenter
```

Planned CMake dependency target:

```text
Center.Thread.Headers
```

Render2D currently wires this through an internal target:

```text
render2d_thread_runtime_support -> Render2D::Render2D + Center.Thread.Headers
```

ThreadCenter will be used by runtime/system pipeline code only. The public `Render2D::Render2D` target and ECS component contracts remain ThreadCenter-free. The single-thread systems remain available and are the correctness reference for threaded execution.

## Stage 10D Verification Commands

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
cmake --preset clang-ninja-perf
cmake --build --preset clang-ninja-perf
ctest --preset clang-ninja-perf
.\scripts\run_null_cpu_benchmarks.ps1 -BuildDir build_perf -IncludeDirtyTransform -Quiet
```

Current result: all commands passed on 2026-06-09. Perf benchmark targets keep standard `RelWithDebInfo` `NDEBUG` codegen; tests add `-UNDEBUG` in release-like builds so assert-based checks remain active.

## Stage 10E Verification Commands

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
clang-tidy -p build bench\null_cpu_bench.cpp tests\transform_dirty_system_test.cpp tests\bounds_system_test.cpp tests\compile_smoke.cpp tests\cpu_system_pipeline_test.cpp --quiet
cmake --build --preset clang-ninja-perf
ctest --preset clang-ninja-perf
.\scripts\run_null_cpu_benchmarks.ps1 -BuildDir build_perf -IncludeDirtyTransform -Quiet
```

Current result: all commands passed on 2026-06-09. Perf capture is recorded in `BENCHMARK_BASELINE.md`.

## Stage 10F Verification Commands

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
clang-tidy -p build bench\null_cpu_bench.cpp tests\draw_sort_system_test.cpp tests\compile_smoke.cpp tests\cpu_system_pipeline_test.cpp --quiet
cmake --build --preset clang-ninja-perf
ctest --preset clang-ninja-perf
.\scripts\run_null_cpu_benchmarks.ps1 -BuildDir build_perf -IncludeDirtyTransform -IncludeSorted -Quiet
```

Current result: all commands passed on 2026-06-09. `--enable-sort` remains explicit because sorting reduces batch count but has measurable CPU cost.

## Stage 10G Verification Commands

```powershell
cmake --preset clang-ninja-debug
cmake --build --preset clang-ninja-debug
ctest --test-dir build --output-on-failure
cmake --preset clang-ninja-perf
cmake --build --preset clang-ninja-perf
ctest --preset clang-ninja-perf
clang-tidy -p build tests\thread_center_dependency_test.cpp tests\compile_smoke.cpp --quiet
git diff --check
clang-tidy --verify-config --config-file=.clang-tidy
```

Current result: build/test passed on 2026-06-09. `render2d_thread_center_dependency_test` proves the ThreadCenter runtime dependency is available without placing ThreadCenter types into ECS components or the public `Render2D::Render2D` interface target.
