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
- [x] 10H: ThreadCenter-backed sprite CPU pipeline runtime is implemented with deterministic chunk merge and single-thread equivalence tests.
- [x] 10I: Upload command coalescing and descriptor table compaction are implemented as allocation-free component-stream systems and benchmarked.
- [x] 10J: Per-thread Vulkan command pools and command recording ownership are implemented in a dedicated native runtime.
- [x] 10K: Final Stage 10 quality gate, documentation, ADR, and project index closeout are complete.

## Remaining Route

None. Stage 10 is complete as of 2026-06-09.

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

## Stage 10H Verification Commands

```powershell
cmake --preset clang-ninja-debug
cmake --build --preset clang-ninja-debug
ctest --test-dir build --output-on-failure
cmake --preset clang-ninja-perf
cmake --build --preset clang-ninja-perf
ctest --preset clang-ninja-perf
.\scripts\run_threaded_cpu_benchmarks.ps1 -BuildDir build_perf -IncludeLarge -Quiet
clang-tidy -p build tests\threaded_cpu_pipeline_test.cpp tests\thread_center_dependency_test.cpp --quiet
git diff --check
clang-tidy --verify-config --config-file=.clang-tidy
```

Current result: build/test/tidy passed on 2026-06-09. `ThreadedCpuPipelineRuntime` parallelizes Transform, Bounds, Culling, and CommandBuild over deterministic chunks, merges visible items in chunk order, and keeps BatchSystem single-threaded as the current correctness-preserving tail stage. The threaded benchmark now records small-workload overhead and large-workload benefit: 10k high-visibility runs are slower on this local run, 10k low-visibility is near parity, and 100k four-worker high/low visibility reaches about 1.11x / 1.11x speedup.

## Stage 10I Verification Commands

```powershell
cmake --preset clang-ninja-debug
cmake --build --preset clang-ninja-debug
ctest --test-dir build --output-on-failure
cmake --preset clang-ninja-perf
cmake --build --preset clang-ninja-perf
ctest --preset clang-ninja-perf
.\build_perf\bench\render2d_upload_descriptor_compaction_bench.exe --items 65536 --frames 8 --warmup 2
clang-tidy -p build tests\upload_descriptor_compaction_test.cpp bench\upload_descriptor_compaction_bench.cpp --quiet
```

Current result: build/test passed on 2026-06-09. The dedicated Perf benchmark reduced 65,536 synthetic upload and descriptor records to 16,384 compacted records; local averages were about 0.183 ms for upload coalescing and 0.133 ms for descriptor compaction.

## Stage 10J Verification Commands

```powershell
cmake --build --preset clang-ninja-debug
ctest --test-dir build --output-on-failure
cmake --build --preset clang-ninja-perf
ctest --preset clang-ninja-perf
clang-tidy -p build tests\vulkan_thread_command_runtime_test.cpp tests\compile_smoke.cpp --quiet
```

Current result: build/test passed on 2026-06-09. `render2d.vulkan_thread_command_runtime` covers invalid config, two per-thread pools, allocation from different pools, begin/end/reset, stale generation rejection, and id reuse.

## Stage 10K Final Quality Gate

```powershell
cmake --preset clang-ninja-debug
cmake --build --preset clang-ninja-debug
ctest --test-dir build --output-on-failure
cmake --preset clang-ninja-perf
cmake --build --preset clang-ninja-perf
ctest --preset clang-ninja-perf
clang-tidy -p build tests\upload_descriptor_compaction_test.cpp tests\vulkan_thread_command_runtime_test.cpp bench\upload_descriptor_compaction_bench.cpp tests\compile_smoke.cpp --quiet
clang-tidy --verify-config --config-file=.clang-tidy
git diff --check
```

Current result: Debug tests passed 30/30 and Perf tests passed 39/39 on 2026-06-09 after benchmark reinforcement. The reinforcement pass adds `render2d_threaded_cpu_pipeline_bench` and the `render2d.threaded_cpu_pipeline_bench_smoke` Perf CTest. Final clang-tidy, `.clang-tidy` verification, `git diff --check`, `std::vector` scan, direct Vulkan memory API scan, and old-math scan passed after reinforcement.
