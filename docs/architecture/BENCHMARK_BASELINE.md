# Benchmark Baseline

Stage 10B establishes reproducible benchmark scenarios before any hot-path optimization. These numbers are local reference data, not hard CI thresholds yet. Future Stage 10 optimization work must record before/after results using the same scenarios.

## Runner

Build the benchmark first:

```powershell
cmake --build build --target render2d_null_cpu_bench
```

Run the standard baseline suite:

```powershell
.\scripts\run_null_cpu_benchmarks.ps1
```

Run the optimized Perf suite:

```powershell
cmake --preset clang-ninja-perf
cmake --build --preset clang-ninja-perf
.\scripts\run_null_cpu_benchmarks.ps1 -BuildDir build_perf -IncludeDirtyTransform
```

Generated files are written under ignored build output:

```text
build/bench_results/null_cpu_baseline_<timestamp>.csv
build/bench_results/null_cpu_baseline_<timestamp>.md
```

Optional large local-only scenarios:

```powershell
.\scripts\run_null_cpu_benchmarks.ps1 -BuildDir build_perf -IncludeLarge
.\scripts\run_null_cpu_benchmarks.ps1 -BuildDir build_perf -IncludeHuge
```

Optional sorted scenarios:

```powershell
.\scripts\run_null_cpu_benchmarks.ps1 -BuildDir build_perf -IncludeSorted
```

## Standard Scenarios

| Scenario | Purpose | Main arguments |
|---|---|---|
| `sprite_high_10k` | Sprite transform/bounds/culling/command baseline with high visibility. | `--scenario sprite --sprites 10000 --visibility high` |
| `sprite_low_10k` | Sprite culling/compaction baseline with low visibility. | `--scenario sprite --sprites 10000 --visibility low` |
| `text_static_2k` | Static text after warmup; validates no dirty glyph rebuild. | `--scenario text --texts 2048 --dirty-text-stride 0` |
| `text_dirty_2k` | Partial dirty text rebuild cadence. | `--scenario text --texts 2048 --dirty-text-stride 8` |
| `mixed_10k_2k` | Combined sprite + text command stream baseline. | `--scenario mixed --sprites 10000 --texts 2048 --dirty-text-stride 8` |

All standard scenarios use `--frames 8 --warmup 2 --glyphs-per-text 8 --format csv` where applicable.

## Current Local Reference Capture

- Captured UTC: 2026-06-09T06:02:38Z
- Build tree: `build`
- Build type: Debug
- Compiler: Clang 22.1.5
- CMake: 4.0.4
- Vulkan SDK: `D:\PUsing\SDKs\Vulkan SDK 1.3.296.0`
- OS: Microsoft Windows NT 10.0.22631.0
- CPU identifier: Intel64 Family 6 Model 154 Stepping 3, GenuineIntel

| Scenario | Visible | Total Draws | Batches | Transform ms | Bounds ms | Culling ms | Sprite Cmd ms | Text Dirty ms | Glyph Run ms | Glyph Instance ms | Glyph Batch ms | Batch ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `sprite_high_10k` | 10000 | 10000 | 79 | 0.10035 | 3.64515 | 0.105675 | 0.130425 | 0 | 0 | 0 | 0 | 0.0547375 |
| `sprite_low_10k` | 1250 | 1250 | 79 | 0.08855 | 3.61521 | 0.0463375 | 0.0161 | 0 | 0 | 0 | 0 | 0.0077875 |
| `text_static_2k` | 0 | 2048 | 2048 | 0 | 0 | 0 | 0 | 0.0281 | 6.25e-05 | 6.25e-05 | 0.0505875 | 0.0194375 |
| `text_dirty_2k` | 0 | 2048 | 2048 | 0 | 0 | 0 | 0 | 0.0331375 | 0.0056875 | 0.06075 | 0.0495 | 0.0227375 |
| `mixed_10k_2k` | 10000 | 12048 | 2127 | 0.101025 | 3.63987 | 0.100638 | 0.129825 | 0.0316125 | 0.0111 | 0.0589625 | 0.0489625 | 0.081325 |

## Initial Bottleneck Reading

- `BoundsSystem` dominates the sprite and mixed baselines, so it is the first data-backed optimization candidate.
- Low visibility reduces command build and batch cost as expected, but bounds remains almost unchanged because all sprite bounds are still transformed.
- Static text validates that warmup removes dirty glyph rebuild work; dirty text shows glyph instance rebuild cost only for the configured partial dirty cadence.
- `BatchSystem` cost is currently small for sprite-only cases but grows with text draw count in mixed scenarios.


## Stage 10C fast_math Migration Result

- Captured UTC: 2026-06-09T07:14:19Z
- Change: `Render2D::Aabb2` / `Affine2X3` structs were removed; `Vec2`, `Mat3`, and `Aabb2` now alias fast_math POD types; transform/bounds/culling/text atlas math now calls fast_math free functions.
- Correctness gate: `ctest --test-dir build --output-on-failure` passed 30/30 after migration.

| Scenario | Transform ms | Bounds ms | Culling ms | Sprite Cmd ms | Text Dirty ms | Glyph Instance ms | Batch ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| `sprite_high_10k` | 0.26095 | 0.5528 | 0.107775 | 0.125225 | 0 | 0 | 0.0577875 |
| `sprite_low_10k` | 0.251775 | 0.531125 | 0.04925 | 0.0149625 | 0 | 0 | 0.00795 |
| `text_static_2k` | 0 | 0 | 0 | 0 | 0.0301125 | 5e-05 | 0.0218375 |
| `text_dirty_2k` | 0 | 0 | 0 | 0 | 0.0346625 | 0.09215 | 0.0251625 |
| `mixed_10k_2k` | 0.262912 | 0.552575 | 0.116475 | 0.1408 | 0.03485 | 0.0939375 | 0.0849125 |

Bounds delta against the Stage 10B capture:

- `sprite_high_10k`: 3.64515 ms -> 0.5528 ms, about 6.6x faster.
- `sprite_low_10k`: 3.61521 ms -> 0.531125 ms, about 6.8x faster.
- `mixed_10k_2k`: 3.63987 ms -> 0.552575 ms, about 6.6x faster.

Transform time increased on this Debug capture because it now uses fast_math trigonometry as required. The bounds bottleneck is removed; future transform work should use a data-backed batch/SoA path instead of reverting to non-fast_math math.


## Stage 10D Benchmark/Profile Harness

Stage 10D adds the measurement coverage required before continuing optimization:

- `clang-ninja-perf` CMake preset using `RelWithDebInfo` and benchmarks enabled.
- Release-like test builds add `-UNDEBUG` under `tests/` only, keeping assert-based tests active while benchmark targets retain standard optimized `NDEBUG` codegen.
- `--dirty-transform-stride <0|N>` benchmark option to mutate a deterministic subset of transforms each frame.
- `scripts/run_null_cpu_benchmarks.ps1 -BuildDir <dir>` so Debug and Perf build trees can be captured separately.
- `-IncludeDirtyTransform`, `-IncludeLarge`, and `-IncludeHuge` runner switches.

Dirty-transform scenario IDs added by `-IncludeDirtyTransform`:

| Scenario | Purpose |
|---|---|
| `sprite_dirty_transform_10k` | 10k sprite pipeline with every fourth transform mutated per frame. |
| `mixed_dirty_transform_10k_2k` | Mixed sprite/text pipeline with partial transform and text dirty updates. |

Large/huge scenario IDs are local-only stress cases and are not hard CI gates yet.

### Stage 10D Debug Capture

- Captured UTC: 2026-06-09T08:12:32Z
- Build tree: `build`
- Command: `.\scripts\run_null_cpu_benchmarks.ps1 -IncludeDirtyTransform -Quiet`
- Correctness gate: `ctest --test-dir build --output-on-failure` passed 31/31.

| Scenario | Dirty Xform | Visible | Total Draws | Batches | Transform ms | Bounds ms | Culling ms | Sprite Cmd ms | Text Dirty ms | Glyph Instance ms | Batch ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `sprite_high_10k` | 0 | 10000 | 10000 | 79 | 0.453438 | 0.953837 | 0.193737 | 0.202563 | 0 | 0 | 0.103187 |
| `sprite_low_10k` | 0 | 1250 | 1250 | 79 | 0.6429 | 1.09659 | 0.120938 | 0.0399 | 0 | 0 | 0.0243125 |
| `text_static_2k` | 0 | 0 | 2048 | 2048 | 0 | 0 | 0 | 0 | 0.0759375 | 0.0001875 | 0.049525 |
| `text_dirty_2k` | 0 | 0 | 2048 | 2048 | 0 | 0 | 0 | 0 | 0.0745375 | 0.161963 | 0.0460875 |
| `mixed_10k_2k` | 0 | 10000 | 12048 | 2127 | 0.655575 | 2.46059 | 0.274575 | 0.247138 | 0.0775625 | 0.1786 | 0.191462 |
| `sprite_dirty_transform_10k` | 4 | 10000 | 10000 | 79 | 0.611013 | 1.00548 | 0.233987 | 0.249262 | 0 | 0 | 0.142275 |
| `mixed_dirty_transform_10k_2k` | 4 | 10000 | 12048 | 2127 | 0.565462 | 0.988237 | 0.205038 | 0.217425 | 0.06565 | 0.14895 | 0.165475 |

### Stage 10D Perf Capture

- Captured UTC: 2026-06-09T08:23:44Z
- Build tree: `build_perf`
- Command: `.\scripts\run_null_cpu_benchmarks.ps1 -BuildDir build_perf -IncludeDirtyTransform -Quiet`
- Correctness gate: `ctest --preset clang-ninja-perf` passed 31/31.

| Scenario | Dirty Xform | Visible | Total Draws | Batches | Transform ms | Bounds ms | Culling ms | Sprite Cmd ms | Text Dirty ms | Glyph Instance ms | Batch ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `sprite_high_10k` | 0 | 10000 | 10000 | 79 | 0.091475 | 0.0445 | 0.0282125 | 0.026525 | 0 | 0 | 0.0254625 |
| `sprite_low_10k` | 0 | 1250 | 1250 | 79 | 0.0917 | 0.044575 | 0.00895 | 0.0037375 | 0 | 0 | 0.0027875 |
| `text_static_2k` | 0 | 0 | 2048 | 2048 | 0 | 0 | 0 | 0 | 0.0113375 | 0.000025 | 0.0052125 |
| `text_dirty_2k` | 0 | 0 | 2048 | 2048 | 0 | 0 | 0 | 0 | 0.0136375 | 0.010125 | 0.005575 |
| `mixed_10k_2k` | 0 | 10000 | 12048 | 2127 | 0.090825 | 0.04445 | 0.0283 | 0.026925 | 0.0116 | 0.0095875 | 0.0307125 |
| `sprite_dirty_transform_10k` | 4 | 10000 | 10000 | 79 | 0.176475 | 0.04445 | 0.0282875 | 0.0272625 | 0 | 0 | 0.025025 |
| `mixed_dirty_transform_10k_2k` | 4 | 10000 | 12048 | 2127 | 0.176675 | 0.0444375 | 0.028325 | 0.0312875 | 0.01185 | 0.0097625 | 0.0308125 |

Perf large/huge switch checks also passed locally:

- `.\scripts\run_null_cpu_benchmarks.ps1 -BuildDir build_perf -IncludeDirtyTransform -IncludeLarge -Quiet` produced 11 scenarios.
- `.\scripts\run_null_cpu_benchmarks.ps1 -BuildDir build_perf -IncludeHuge -Quiet` produced 7 scenarios.

## Stage 10E Single-Thread Transform/Bounds Result

Stage 10E adds:

- `TransformDirtyItem` as a Strict POD ECS-visible dirty-index component.
- `TransformSystem::runDirty` and `BoundsSystem::runDirty` for sparse transform/bounds updates.
- a zero-rotation `TransformSystem` fast path that avoids trigonometry for common static sprites.

Culling and command-build loops were measured, but no code change was retained there because the attempted micro-optimization did not show stable benefit.

- Captured UTC: 2026-06-09T08:56:21Z
- Build tree: `build_perf`
- Command: `.\scripts\run_null_cpu_benchmarks.ps1 -BuildDir build_perf -IncludeDirtyTransform -Quiet`
- Correctness gate: `ctest --preset clang-ninja-perf` passed 32/32.

| Scenario | Dirty Xform | Transform ms | Bounds ms | Culling ms | Sprite Cmd ms | Batch ms |
|---|---:|---:|---:|---:|---:|---:|
| `sprite_high_10k` | 0 | 0.026075 | 0.0447875 | 0.03075 | 0.0291125 | 0.0277875 |
| `sprite_low_10k` | 0 | 0.0238125 | 0.042875 | 0.0093875 | 0.0067625 | 0.0029375 |
| `mixed_10k_2k` | 0 | 0.0304875 | 0.0454875 | 0.031175 | 0.031575 | 0.0343125 |
| `sprite_dirty_transform_10k` | 4 | 0.117062 | 0.0216125 | 0.0316 | 0.0301125 | 0.029975 |
| `mixed_dirty_transform_10k_2k` | 4 | 0.097375 | 0.015925 | 0.0294 | 0.029425 | 0.03295 |

Perf delta against the Stage 10D Perf capture:

- Static 10k transform: about 3.0x-3.9x faster from zero-rotation fast path.
- Dirty 10k transform: about 1.5x-1.8x faster from dirty-index updates.
- Dirty 10k bounds: about 2.1x-2.8x faster from dirty-index updates.

## Stage 10F Sort/Batch Key Result

Stage 10F adds:

- `makeDrawSortKey(layer, material, texture, flags)` packed key helper.
- `DrawSortSystem` stable 4-pass radix sort over `DrawCommand.sort_key`.
- Batch merge first compares packed `sort_key`, then verifies full material/texture/layer/flags to avoid collision merges.
- Benchmark `--enable-sort` and runner `-IncludeSorted`; sorting remains explicit because sprite-only CPU paths can be faster without sorting.

- Captured UTC: 2026-06-09T09:32:04Z
- Build tree: `build_perf`
- Command: `.\scripts\run_null_cpu_benchmarks.ps1 -BuildDir build_perf -IncludeDirtyTransform -IncludeSorted -Quiet`
- Correctness gate: `ctest --preset clang-ninja-perf` passed 33/33.

| Scenario | Sort | Total Draws | Batches | Sort ms | Batch ms |
|---|---:|---:|---:|---:|---:|
| `sprite_high_10k` | 0 | 10000 | 79 | 0 | 0.0185125 |
| `text_dirty_2k` | 0 | 2048 | 2048 | 0 | 0.002825 |
| `mixed_10k_2k` | 0 | 12048 | 2127 | 0 | 0.0189875 |
| `sprite_sorted_10k` | 1 | 10000 | 8 | 0.18185 | 0.0185 |
| `text_sorted_2k` | 1 | 2048 | 16 | 0.025725 | 0.0034625 |
| `mixed_sorted_10k_2k` | 1 | 12048 | 24 | 0.19365 | 0.019925 |

Interpretation:

- Sorting reduces batch count dramatically: sprite 79 -> 8, text 2048 -> 16, mixed 2127 -> 24.
- CPU-only sort cost is visible, so sorting is now an explicit runtime choice instead of always-on.
- Native renderer stages can use sorted paths when reduced batch/native submit count outweighs sort cost.

## Stage 10H Threaded CPU Pipeline Result

Stage 10H adds `ThreadedCpuPipelineRuntime`, a ThreadCenter-backed sprite CPU pipeline facade. The benchmark now records reference single-thread pipeline time against the threaded runtime using the same component streams.

Build and run:

```powershell
cmake --preset clang-ninja-perf
cmake --build --preset clang-ninja-perf
.\scripts\run_threaded_cpu_benchmarks.ps1 -BuildDir build_perf -IncludeLarge -Quiet
```

- Captured UTC: 2026-06-09T12:39:10Z
- Build tree: `build_perf`
- Correctness gate: benchmark verifies reference/threaded visible/draw/batch counts match per frame.

| Scenario | Sprites | Visibility | Workers | Visible | Draws | Batches | Reference ms | Threaded ms | Speedup |
|---|---:|---|---:|---:|---:|---:|---:|---:|---:|
| `threaded_sprite_high_10k_w1` | 10000 | high | 1 | 10000 | 10000 | 79 | 0.161787500 | 0.311262500 | 0.519778322 |
| `threaded_sprite_high_10k_w4` | 10000 | high | 4 | 10000 | 10000 | 79 | 0.166350000 | 0.396900000 | 0.419123205 |
| `threaded_sprite_low_10k_w4` | 10000 | low | 4 | 1250 | 1250 | 79 | 0.109525000 | 0.106937500 | 1.024196376 |
| `threaded_sprite_high_100k_w4` | 100000 | high | 4 | 100000 | 100000 | 782 | 1.968775000 | 1.779437500 | 1.106403007 |
| `threaded_sprite_low_100k_w4` | 100000 | low | 4 | 12500 | 12500 | 782 | 1.004175000 | 0.901937500 | 1.113353198 |

Interpretation:

- 10k high-visibility workloads are slower through ThreadCenter because scheduling, chunk scratch, and merge overhead dominate; 10k low-visibility is near parity on this local run and should be treated as noise-sensitive.
- 100k sprite workloads begin to benefit: about 1.11x high visibility and 1.11x low visibility on this local run.
- The threaded runtime should remain opt-in and threshold-gated by workload size; the single-thread systems remain the small-workload reference path.

## Stage 10I Upload/Descriptor Compaction Result

Stage 10I adds:

- `UploadCoalesceSystem` for adjacent `UploadCommand[]` byte-range merging.
- `DescriptorCompactionSystem` for adjacent `DescriptorSlice[]` descriptor-table range merging.
- `render2d_upload_descriptor_compaction_bench` for a dedicated Perf smoke benchmark.

The benchmark uses synthetic four-record groups so the expected output count is exactly one compacted record per group.

- Captured UTC: 2026-06-09
- Build tree: `build_perf`
- Command: `.\build_perf\bench\render2d_upload_descriptor_compaction_bench.exe --items 65536 --frames 8 --warmup 2`
- Correctness gate: `ctest --preset clang-ninja-perf` passed 38/38.

| Items | Upload Output | Descriptor Output | Upload Coalesce ms | Descriptor Compaction ms |
|---:|---:|---:|---:|---:|
| 65536 | 16384 | 16384 | 0.182750000 | 0.132950000 |

Interpretation:

- Both systems reduce the synthetic stream by 4x.
- The systems are CPU-only, allocation-free, and keep outputs as ECS component streams.
- The benchmark is a smoke/perf guard for stream compaction cost, not a GPU throughput claim.

## Gate Rule

Before any Stage 10 optimization is accepted:

1. Run this baseline suite before the change.
2. Apply one optimization only.
3. Run the same suite after the change.
4. Record delta and correctness verification.
5. Keep `ctest`, clang-tidy, `std::vector` scan, and direct Vulkan memory API scan green.

## Stage 21 Automated Performance-Regression Gate

Stage 21 promotes the manual "Gate Rule" above into an automated CTest gate. The
benchmark harness gained a perf-gate mode (`bench/support/BenchmarkFramework.hpp`):
after the normal report, `null_cpu_bench` evaluates any expectation flags and exits
non-zero on violation. The gate has two deliberately separated layers.

### Layer 1 - deterministic work-count expectations (always on, never flaky)

The pipeline produces identical visible / total-draw / glyph-draw / batch counts on
every machine and every run, so these are the precise regression net: a broken
culling, batch-merge, or sort changes the exact integer. Flags:
`--expect-visible`, `--expect-total-draws`, `--expect-glyph-draws`, `--expect-batches`.

### Layer 2 - generous wall-clock catastrophe budget

`--max-total-avg-ms <ms>` asserts the summed per-frame stage time stays under a
budget. Wall-clock is machine-dependent, so the budget (`RENDER2D_PERF_GATE_MAX_TOTAL_AVG_MS`,
default `25`) is set far above any real baseline. It catches O(n^2)-class slowdowns
or hot-loop allocations (which run into hundreds of ms at 10k items even optimized),
not micro-regressions, so it never flakes on shared CI runners. It is honest about
what it does and does not prove: a green gate guarantees correct output structure and
"no catastrophic slowdown", **not** "no small regression" - those still need the
manual before/after capture above.

### CTest cases (`render2d.perf_gate_*`, Perf preset only)

Benchmarks are OFF in the Debug preset, so the gate cases build and run only under
`clang-ninja-perf` (optimized RelWithDebInfo) and ride the existing `Test (Perf)` CI
step automatically. Expected counts (frozen here; identical to the captures above):

| Gate case | Scenario | Visible | Total draws | Glyph draws | Batches |
|---|---|---:|---:|---:|---:|
| `perf_gate_sprite_high` | sprite 10k high | 10000 | 10000 | - | 79 |
| `perf_gate_sprite_low` | sprite 10k low | 1250 | 1250 | - | 79 |
| `perf_gate_sprite_sorted` | sprite 10k high, sorted | 10000 | 10000 | - | 8 |
| `perf_gate_text` | text 2048 x8 | 0 | 2048 | 2048 | 2048 |
| `perf_gate_text_sorted` | text 2048 x8, sorted | 0 | 2048 | 2048 | 16 |
| `perf_gate_mixed` | mixed 10k/2048, dirty-text 8 | 10000 | 12048 | 2048 | 2127 |
| `perf_gate_mixed_sorted` | mixed, sorted | 10000 | 12048 | 2048 | 24 |

- Verified UTC: 2026-06-14. Build tree: `build_perf` (Clang 22, RelWithDebInfo).
- Correctness gate: `ctest --preset clang-ninja-perf` passed 68/68 (61 prior + 7 gate cases).
- Each gate case completes in ~0.01 s, far under the 25 ms budget.

When a future stage intentionally changes a count (e.g. parallel batching that alters
the batch total, or a sort change), update the expected value in `bench/CMakeLists.txt`
**and** the table above in the same commit, with the before/after recorded.

## Stage 21A Parallel Glyph Instance Build

Stage 21A parallelizes the dominant text-path stage. The text path was profiled at
scale (RelWithDebInfo, Clang 22) to find the data-backed target rather than
parallelizing blindly:

| texts (glyphs) | text_dirty ms | glyph_run ms | **glyph_instance ms** | glyph_batch ms |
|---|---:|---:|---:|---:|
| 8192 (65k) | 0.071 | 0.027 | **0.344** | 0.070 |
| 32768 (262k) | 0.276 | 0.134 | **1.515** | 0.242 |
| 131072 (1M) | 1.223 | 0.511 | **6.105** | 1.074 |

`GlyphInstanceBuildSystem::runDirty` dominates (≈70% of text-path time at 1M glyphs)
and scales linearly, while the other stages stay small. It is also the only text
stage that is embarrassingly parallel: each dirty range writes a disjoint glyph
slice (`glyph_first .. glyph_first + glyph_count`), so no prefix-sum or compaction
merge is needed. The cheaper prefix-sum (`TextDirtySystem`) and compaction
(`GlyphBatchSystem`) stages remain the single-thread reference.

`ThreadedTextCpuPipelineRuntime::runGlyphInstanceBuildDirty` partitions the dirty
ranges across workers and reuses `GlyphInstanceBuildSystem::runDirty` per chunk; the
output is byte-identical to the single-thread system (verified by
`render2d.threaded_text_cpu_pipeline`, a `memcmp` equivalence test). It is gated by
the shared Stage 21E threshold (`ParallelPolicy.hpp`), so sub-threshold workloads run
single-threaded.

Threaded vs single-thread, `render2d_threaded_text_cpu_pipeline_bench --workers 4
--min-glyphs-per-task 8192 --frames 8 --warmup 2` (RelWithDebInfo, Clang 22):

| glyphs | reference ms | threaded ms | speedup |
|---:|---:|---:|---:|
| 262144 | 1.493 | 0.636 | 2.35x |
| 1048576 | 5.970 | 2.955 | 2.02x |
| 2097152 | 12.000 | 6.212 | 1.93x |

Interpretation:

- A consistent ~2x speedup with 4 workers at large glyph counts, byte-identical to the
  reference. This is a large-text-scene optimization (e.g. dense document/log/terminal
  rendering); small text loads stay single-threaded via the threshold gate.
- The runtime parallelizes only `GlyphInstanceBuild`. The other text stages were
  measured small and were intentionally left single-threaded (no data-backed reason to
  pay ThreadCenter overhead for them); revisit if a future workload makes them hot.

## Stage 21B Parallel Deterministic Draw Sort

Stage 21B parallelizes the sorted-path tail. The tail is two stages —
`DrawSortSystem` (stable 4-pass LSD radix sort) then `BatchSystem` (adjacent-merge
scan). The Stage 10F sorted capture above is the data: at 10–12k draws the sort is
`0.18–0.19 ms` (the single largest stage in the sorted path: transform `0.09`,
bounds `0.04`, batch `0.02`), and it grows with draw count. So Stage 21B
parallelizes **only the radix sort**. `BatchSystem` is both tiny (`~0.02 ms`) and
inherently sequential (a running batch-index merge scan), so there is no
data-backed reason to pay ThreadCenter overhead for it; it stays the single-thread
reference.

`ThreadedDrawSortRuntime::runDrawSort` is byte-identical to `DrawSortSystem::run`
(verified by `render2d.threaded_draw_sort`, a `memcmp` equivalence test, and by a
per-frame `memcmp` in the bench). Determinism comes from the offset pass computing
the global scatter position for every (bucket, chunk) in **bucket-major then
chunk-order**: all of bucket *b*'s items precede bucket *b+1*'s, and within a bucket
chunk 0's items precede chunk 1's. Because chunks are contiguous in source order,
this reproduces the serial radix's increasing-source-index order per bucket — the
same stable permutation. Each chunk then scatters into its own disjoint offset
cursors. Gated by the shared Stage 21E threshold (`ParallelPolicy.hpp`), so
sub-threshold workloads run single-threaded. See ADR
`2026-06-15-stage21-parallel-deterministic-draw-sort.md`.

- Captured UTC: 2026-06-15. Build tree: `build_perf` (Clang 22, RelWithDebInfo).
- CPU: 22 logical cores (auto worker count = cores − 2 = 20).
- Correctness gate: `ctest --preset clang-ninja-perf` passed 72/72; the bench
  verifies reference/threaded byte-equality every frame.

`render2d_threaded_draw_sort_bench --workers 4 --min-items-per-task 4096 --frames 12
--warmup 4` (4 workers, matching the 21A capture):

| draws | reference ms | threaded ms | speedup |
|---:|---:|---:|---:|
| 65536 | 1.086 | 0.785 | 1.38x |
| 262144 | 6.925 | 3.468 | 2.00x |
| 524288 | 16.611 | 7.623 | 2.18x |
| 1048576 | 37.759 | 15.959 | 2.37x |
| 2097152 | 78.631 | 32.617 | 2.41x |

Auto worker count (20 workers, same flags without `--workers`):

| draws | reference ms | threaded ms | speedup |
|---:|---:|---:|---:|
| 1048576 | 37.188 | 11.485 | 3.24x |
| 2097152 | 75.064 | 23.916 | 3.14x |

Interpretation:

- Scaling rises with draw count (work per worker grows relative to the fixed
  scheduling cost) and plateaus near `~2.4x` at 4 workers / `~3.2x` at 20. The cap is
  inherent to a parallel radix: the per-pass offset (prefix-sum) stage is a single
  serial task, and the seed/gather passes add memory-bandwidth-bound overhead the
  serial sort does not pay. This is the expected Amdahl ceiling, not a defect.
- A small workload (the sorted tail at 10–12k draws) stays single-threaded via the
  threshold gate; the win is for large sorted scenes (tens of thousands to millions
  of draws), where the radix sort is the dominant CPU cost.
- `BatchSystem` was left single-threaded by design (measured tiny and sequential);
  revisit only if a future workload makes the merge scan hot.

## Stage 21 At-Scale CPU Bottleneck Map

The earlier captures stop at 10k draws, where every sprite stage is sub-`0.05 ms`
and no bottleneck is visible. A Stage 21 sweep at 100k–2M draws was run to decide
the remaining (data-driven, optional) 21C/21D SIMD/SoA work on real numbers rather
than the 10k reading.

- Captured UTC: 2026-06-15. Build tree: `build_perf` (Clang 22, RelWithDebInfo).
- CPU: 22 logical cores (auto worker count = 20). `--frames 8 --warmup 2`.

### Per-stage single-thread cost, high visibility (ms)

Static (`rotation = 0`, so `TransformSystem`'s no-trig fast path):

| sprites | transform | bounds | culling | sprite_cmd | batch |
|---:|---:|---:|---:|---:|---:|
| 10000 | 0.024 | 0.040 | 0.027 | 0.035 | 0.029 |
| 100000 | 0.274 | 0.415 | 0.286 | 0.403 | 0.315 |
| 500000 | 2.116 | 2.465 | 1.648 | 3.159 | 2.470 |
| 1000000 | 4.291 | 5.188 | 3.324 | 6.579 | 5.349 |
| 2000000 | 8.170 | 9.452 | 6.109 | 12.995 | 10.784 |

Rotating (`--dirty-transform-stride 1`: every transform dirty + nonzero rotation,
so the `MMath::sincos` trig path):

| sprites | transform | bounds | culling | sprite_cmd | batch |
|---:|---:|---:|---:|---:|---:|
| 10000 | 0.361 | 0.043 | 0.027 | 0.037 | 0.030 |
| 100000 | 3.768 | 0.461 | 0.285 | 0.496 | 0.349 |
| 500000 | 21.810 | 3.194 | 1.709 | 3.250 | 2.771 |
| 1000000 | 41.387 | 5.955 | 3.440 | 6.527 | 5.245 |
| 2000000 | 102.749 | 16.682 | 9.288 | 15.705 | 14.844 |

Single-thread sort (`--enable-sort`): `3.17 ms` at 100k, `36.9 ms` at 1M,
`69.9 ms` at 2M — the dominant sorted-path stage (addressed by Stage 21B).

### Whole sprite pipeline threaded (Stage 10H) speedup vs single-thread

`render2d_threaded_cpu_pipeline_bench --visibility high --min-items-per-task 4096`
(a `--rotate` flag was added to this bench to cover the compute-bound case the
static fill never exercised):

| case | 4 workers | 20 workers (auto) |
|---|---:|---:|
| static (1M) | 1.16x | 1.34x |
| static (2M) | 1.16x | 1.27x |
| rotating (1M) | 2.07x | 2.89x |
| rotating (2M) | 2.04x | 2.88x |

### Reading and the 21C/21D decision

- **Compute-bound: the rotating `TransformSystem` (trig).** It is by far the
  largest stage at scale (`41 ms` at 1M, `103 ms` at 2M) and the only one that
  parallelizes well (`~2.9x` at 20 workers). The runtime already parallelizes it
  (`ThreadedCpuPipelineRuntime::runSpatialAndCulling`); the further lever is SIMD
  `sincos`, which belongs in **fast_math** (the math invariant forbids
  Render2D-local trig), not in this repo. SoA transform columns (21D) would feed
  that SIMD, but component storage is the **host ECS's** to choose (out of scope).
  → 21D's actionable part is a fast_math batched-`sincos` request; not a
  Render2D-local change.
- **Bandwidth-bound: bounds, culling, command-build, batch.** These are linear
  passes over POD streams; whole-pipeline threading plateaus at `~1.3x` because
  20 workers ≈ 4 workers — the signature of a memory-bandwidth wall. SIMD targets
  ALU, not bandwidth, so **21C (SIMD bounds/culling) is declined**: there is no
  data-backed win.
- The one bandwidth-bound stage with a worthwhile parallel win is `batch`, because
  its dominant cost is the key-comparison scan rather than the output writes — see
  the next section (Stage 21B batch tail).

## Stage 21B Parallel Deterministic Batch

The at-scale map above shows `BatchSystem` is `5–15 ms` at 1–2M draws, not the
`~0.02 ms` the 10k-era Stage 21B sort ADR assumed when it left batch
single-threaded. `ThreadedBatchRuntime` (`include/Render2D/System/ThreadedBatchSystem.hpp`)
parallelizes it with a segmented start-based scan that is byte-identical to
`BatchSystem::run` / `runBindless` (verified by `render2d.threaded_batch_system`
and a per-frame `memcmp` in the bench). See ADR
`2026-06-15-stage21-parallel-deterministic-batch.md`.

- Captured UTC: 2026-06-15. Build tree: `build_perf` (Clang 22, RelWithDebInfo).
- CPU: 22 logical cores (auto worker count = 20). `--min-items-per-task 4096
  --frames 12 --warmup 4`.
- Correctness gate: `ctest --preset clang-ninja-perf` passed 74/74; the bench
  verifies reference/threaded byte-equality every frame.

`render2d_threaded_batch_bench` at auto (20) workers. `run_length` is draws per
batch run: `1` is the degenerate no-merge stream (every draw its own batch), large
values model the sorted/clustered stream where batching is actually used:

| run_length (regime) | draws | reference ms | threaded ms | speedup |
|---|---:|---:|---:|---:|
| 1 (degenerate) | 1000000 | 5.789 | 3.561 | 1.63x |
| 1 (degenerate) | 2000000 | 12.746 | 7.542 | 1.69x |
| 8 | 1000000 | 4.043 | 1.334 | 3.03x |
| 8 | 2000000 | 8.210 | 2.745 | 2.99x |
| 128 (sorted/clustered) | 1000000 | 4.353 | 1.045 | 4.16x |
| 128 (sorted/clustered) | 2000000 | 8.714 | 2.068 | 4.21x |

Interpretation:

- On the realistic sorted/clustered stream (few long runs) the win is `3–4x`,
  because the dominant cost is the parallel start-finding scan and few
  `BatchCommand`s are written. The degenerate no-merge stream is write-bound (one
  48-byte `BatchCommand` per draw) and reaches only `~1.6x` — the same
  bandwidth ceiling the other stream stages hit.
- Gated by the shared Stage 21E threshold, so the small sorted tail at 10–12k
  draws stays single-threaded; the win is for large sorted scenes.
