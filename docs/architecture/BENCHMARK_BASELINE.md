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
