# Null CPU Benchmark

The Null CPU benchmark is the Stage 10A baseline gate for CPU-side ECS component pipelines. It does not create Vulkan objects, does not record GPU commands, and does not depend on the temporary test ECS storage. All benchmark-owned dynamic arrays use `Render2D::McVector`.

## Build

Enable benchmark targets explicitly:

```powershell
cmake --preset clang-ninja-debug -DRENDER2D_BUILD_BENCHMARKS=ON
cmake --build build
```

## Run

```powershell
.\build\bench\render2d_null_cpu_bench.exe --scenario mixed --sprites 10000 --texts 2048 --frames 8 --warmup 2
```

Useful options:

```text
--scenario sprite|text|mixed
--sprites <count>
--texts <count>
--frames <count>
--warmup <count>
--visibility high|low
--glyphs-per-text <count>
--dirty-text-stride <0|N>
--format text|csv
```

CTest smoke cases cover sprite, text, mixed text output, and mixed CSV output:

```text
render2d.null_cpu_bench_smoke
render2d.null_cpu_bench_sprite_smoke
render2d.null_cpu_bench_text_smoke
render2d.null_cpu_bench_mixed_csv_smoke
```

## Covered Pipelines

Sprite path:

```text
Transform[] -> WorldTransform[] -> WorldBounds[] -> VisibleItem[] -> DrawCommand[]
```

Text path:

```text
Text[] + TextState[] + FontAtlasRef[]
  -> TextDirtyRange[]
  -> GlyphRun[]
  -> GlyphInstance[]
  -> DrawCommand[]
```

Shared path:

```text
DrawCommand[] -> BatchCommand[] -> CommandBuffer[]
```

The mixed scenario appends text draw commands after sprite draw commands, then batches the combined command stream. Static text can be measured with `--dirty-text-stride 0`; incremental dirty updates can be measured with `--dirty-text-stride N`. Numeric arguments are parsed strictly: malformed values fail instead of falling back to defaults.

## Reported Metrics

The report includes active counts and average per-frame timings for:

- transform, bounds, culling, and sprite command build
- text dirty detection, glyph run update, glyph instance update, and glyph batching
- final batch build and command buffer descriptor build

Stage 10 optimizations must compare against this baseline before changing hot-path implementations.
