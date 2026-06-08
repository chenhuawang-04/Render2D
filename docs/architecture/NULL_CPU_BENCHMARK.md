# Null CPU Benchmark

Stage 6 validates the CPU-side ECS component pipeline without Vulkan, GPU resources, or engine ECS integration.

## Build

Enable benchmark targets explicitly:

```powershell
cmake --preset clang-ninja-debug -DRENDER2D_BUILD_BENCHMARKS=ON
cmake --build build
```

## Run

```powershell
.\build\bench\render2d_null_cpu_bench.exe --sprites 10000 --frames 4
```

CTest smoke is registered as:

```text
render2d.null_cpu_bench_smoke
```

## Covered Pipeline

```text
Transform[]
  -> WorldTransform[]
  -> WorldBounds[]
  -> VisibleItem[]
  -> DrawCommand[]
  -> BatchCommand[]
  -> CommandBuffer[]
```

The benchmark uses deterministic data. Containers are only used to hold benchmark component arrays. Production systems still consume component streams via `std::span` and do not depend on the temporary test ECS storage.

## Reported Metrics

- visible count
- draw count
- batch count
- average transform pass milliseconds
- average bounds pass milliseconds
- average culling pass milliseconds
- average command build milliseconds
- average batch pass milliseconds
- average command buffer descriptor milliseconds
