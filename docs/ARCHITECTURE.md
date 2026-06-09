# Render2D Architecture

Render2D is a C++23, component-first, Vulkan-native rendering module. The current implementation keeps ECS-visible data as Strict POD streams while native runtimes own Vulkan object lifetimes behind `id + generation` references.

## Core principles

1. **ECS owns components.** All render data records are ECS components, including `VisibleItem`, `DrawCommand`, `BatchCommand`, `UploadCommand`, and `NativeSubmitCommand`.
2. **Components are Strict POD.** Components are trivial, standard-layout, trivially copyable, and aggregate. They do not own resources and do not contain `std::string`, `std::vector`, `std::span`, RAII wrappers, constructors, destructors, or virtual functions.
3. **Systems do not own ECS storage.** Production systems consume and write component streams through non-owning boundaries such as `std::span`.
4. **Provider/Dim are compile-time tags.** The current valid domain is `VulkanNativeProvider + Dim2`.
5. **Memory is centralized.** Render2D-owned dynamic CPU arrays use `Render2D::McVector` backed by MemoryCenter/Vector_New; Vulkan buffer/image backing memory is allocated and synchronized through `VulkanMemoryCenterAllocator`.
6. **Math is centralized through fast_math.** Render2D math aliases (`Vec2`, `Mat3`, `Aabb2`) are `MMath` Strict POD types, and render math systems call fast_math free functions.
7. **Native runtime owns backend lifetimes.** ECS stores POD handles/IDs. Runtime tables own and validate backend slots behind those refs.
8. **Stream sizes are U32-bounded.** Component indices, ranges, and `SystemResult` counts are `U32`; systems reject component streams whose sizes cannot be represented by `U32`.

## Current data pipeline

```text
Sprite path:
Transform[] / Sprite[] / Camera[]
    -> TransformSystem
    -> BoundsSystem
    -> CullingSystem
    -> CommandBuildSystem
    -> DrawCommand[]

Text path:
Text[] + TextState[] + FontAtlasRef[]
    -> TextDirtySystem
    -> TextDirtyRange[]
    -> GlyphRunBuildSystem
    -> GlyphInstanceBuildSystem
    -> GlyphBatchSystem
    -> DrawCommand[]

DrawCommand[]
    -> DrawSortSystem (optional)
    -> DrawCommand[]
    -> BatchSystem
    -> BatchCommand[]

Native-bound side streams:
UploadCommand[]
    -> UploadCoalesceSystem
    -> UploadCommand[]
DescriptorSlice[]
    -> DescriptorCompactionSystem
    -> DescriptorSlice[]

BatchCommand[] + UploadCommand[] + DescriptorSlice[]
    -> CommandBufferBuildSystem
    -> CommandBuffer[]
    -> EncodeSystem
    -> NativeCommandBufferRef[]
    -> SubmitSystem
    -> NativeSubmitCommand[]
```

The CPU component pipeline remains ECS-driven. Stage 8 now attaches Vulkan command, sync, submit, resource, descriptor, pipeline, upload-ring, and dynamic-rendering encoder runtimes behind POD references. The offscreen smoke path records an indirect full-screen triangle draw and validates readback.

## Component layer

The component layer defines Strict POD ECS records:

- Scene/input components: `Transform`, `Sprite`, `Text`, `Utf8Slice`, `Camera`, `LocalBounds`, `VisibilityMask`, `RenderLayer`, `MaterialRef`, `TextureRef`, `FontRef`, `FontAtlasRef`.
- Derived components: `WorldTransform`, `WorldBounds`, `VisibleItem`, `SortedItem`, `TextState`, `TextDirtyRange`, `GlyphRun`, `GlyphInstance`.
- Command components: `DrawCommand`, `BatchCommand`, `UploadCommand`, `NativeSubmitCommand`, `CommandBuffer`.
- Frame/native state components: `FrameIndex`, `FrameArenaState`, `DescriptorSlice`, `UploadRingSlice`, `FenceState`.
- Native resource references: `DeviceHandle`, `QueueHandle`, `SwapchainState`, `FrameSync`, `NativeCommandBufferRef`, `PipelineRef`, `ImageRef`, `BufferRef`, `UploadSlice`.

Every supported component is registered through `ComponentTraits` and checked by `SupportedRenderComponent`. `WorldTransform` stores `Render2D::Mat3` (`MMath::Mat3`), while `LocalBounds`, `WorldBounds`, and `GlyphInstance::atlas_rect` store `Render2D::Aabb2` (`MMath::Aabb2`). Use `makeAabb2` / `aabb2Min` / `aabb2Max` instead of touching AABB internals.

## System layer

Systems are stateless template types. They:

- take input component streams as `std::span<const T>`;
- take output component streams as `std::span<T>`;
- return `SystemResult`;
- do not allocate in the hot path;
- do not depend on ECS storage implementation;
- do not call Vulkan.

This keeps systems reusable when the temporary test ECS is replaced by the host engine ECS.

Stage 9 text systems are dependency-free and deterministic. `TextDirtySystem` emits dirty glyph ranges, `GlyphRunBuildSystem` and `GlyphInstanceBuildSystem` can update only those ranges, and `GlyphBatchSystem` emits regular `DrawCommand[]` entries over `GlyphInstance[]`. Real UTF-8 decoding, shaping, rasterization, atlas packing, and FreeType linkage are deferred to a dedicated font runtime stage.

Stage 10C migrated transform, bounds, culling, and atlas-rect math to fast_math. `BoundsSystem` now uses the fast center/extents transform formula over `MMath::Mat3` / `MMath::Aabb2` instead of Render2D-owned math structs.

Stage 10F adds packed draw sort keys and an optional stable radix `DrawSortSystem`. Sorting reduces batch counts when command streams are not already resource-grouped, but it is explicit (`--enable-sort` in benchmarks) because CPU-only sort cost is measurable.

Stage 10G integrates ThreadCenter as runtime/system infrastructure only. The repository now embeds `Center.Thread.Headers` and exposes it through an internal `render2d_thread_runtime_support` target used by a smoke test. ECS components, system signatures, and the public `Render2D::Render2D` interface remain ThreadCenter-free at this stage.

Stage 10H adds `ThreadedCpuPipelineRuntime`, a ThreadCenter-backed runtime facade for the sprite CPU path. It parallelizes Transform, Bounds, Culling, and CommandBuild over fixed chunks, writes per-chunk culling scratch with `McVector`, merges visible items in chunk order, then runs BatchSystem through the existing single-thread reference path. It does not add ECS components and is intentionally not included by the umbrella header because it requires the internal ThreadCenter support target.

Stage 10I adds allocation-free stream compaction for native-bound data. `UploadCoalesceSystem` merges adjacent contiguous `UploadCommand[]` records with the same resource/kind/flags, and `DescriptorCompactionSystem` merges adjacent contiguous `DescriptorSlice[]` records with the same descriptor set/generation. These outputs remain ECS component streams and are not native storage.

Stage 10J adds `VulkanThreadCommandRuntime`, a Vulkan-native runtime that owns one command pool per runtime thread slot. It allocates command buffers from the requested thread pool and returns ordinary `NativeCommandBufferRef` id/generation records; thread ownership stays in runtime metadata and does not enter ECS components.

## Temporary test ECS

The repository includes test-only storage under `tests/support/`. This storage exists only to validate components and systems. It is not production architecture and must be replaced by the host engine ECS during integration. Its backing arrays use `Render2D::McVector`, but the storage itself remains test-only.

## Native runtime

Native references exposed to ECS use compact IDs plus generation counters:

```text
resource_id + generation
```

Examples:

```text
DeviceHandle::device_id + DeviceHandle::generation
QueueHandle::queue_id + QueueHandle::generation
SwapchainState::swapchain_id + SwapchainState::generation
PipelineRef::pipeline_id + PipelineRef::generation
ImageRef::image_id + ImageRef::generation
BufferRef::buffer_id + BufferRef::generation
DescriptorSlice::descriptor_set_id + DescriptorSlice::generation
NativeCommandBufferRef::command_buffer_id + NativeCommandBufferRef::generation
UploadRingSlice::ring_id + UploadRingSlice::generation
```

Implemented CPU-side runtime skeletons:

- `NativeFrameRuntime` - frame-in-flight slot rotation and `FrameSync` output.
- `NativeDeviceRuntime` - device/queue handle slot tables.
- `NativeResourceRuntime` - buffer/image reference slot tables.
- `NativePipelineRuntime` - pipeline reference slot table.
- `NativeDescriptorRuntime` - descriptor slice slot table.
- `NativeSwapchainRuntime` - swapchain state slot table and resize generation bump.
- `NativeCommandRuntime` - CPU-only native command buffer reference slot table.
- `NativeDeferredDestroyRuntime` - runtime-owned queue for frame-safe deferred resource retirement.

Implemented Vulkan-backed runtimes:

- `VulkanCommandRuntime` - Vulkan command pool and command buffer lifecycle owner behind `NativeCommandBufferRef`.
- `VulkanThreadCommandRuntime` - per-thread Vulkan command pools and command buffer lifecycle ownership behind `NativeCommandBufferRef`.
- `VulkanSyncRuntime` - real semaphore/fence lifecycle behind `FrameSync`.
- `VulkanSubmitRuntime` - real `vkQueueSubmit` using resolved command buffers and frame sync.
- `VulkanResourceRuntime` - real buffer/image/image-view lifecycle, MemoryCenter-backed GPU allocation, upload/readback, copies, and image layout tracking.
- `VulkanDescriptorRuntime` - descriptor pool, descriptor set layout, set allocation, and descriptor array updates.
- `VulkanPipelineRuntime` - shader module creation, pipeline cache, dynamic-rendering pipeline layout/pipeline creation.
- `VulkanUploadRingRuntime` - MemoryCenter-backed persistent mapped, frame-segmented upload ring; slices are not reusable until the frame is completed.
- `VulkanDynamicRenderEncoder` - records dynamic rendering, pipeline bind, direct draw, and indirect draw.

These runtime classes are not ECS storage. They own backend slot lifecycle metadata, validate generations, reject stale references, reuse slots, and keep Vulkan handles out of ECS components.

## Benchmarking

The Null CPU benchmark validates the current sprite/text/mixed component pipelines without Vulkan:

```powershell
cmake --preset clang-ninja-debug -DRENDER2D_BUILD_BENCHMARKS=ON
cmake --build build
.\build\bench\render2d_null_cpu_bench.exe --scenario mixed --sprites 10000 --texts 2048 --frames 8 --warmup 2
```

It reports active counts and average pass timings for sprite systems, text dirty/glyph systems, batching, and command buffer descriptor build. The Stage 10B standard suite is run with `scripts/run_null_cpu_benchmarks.ps1` and documented in `docs/architecture/BENCHMARK_BASELINE.md`. Stage 10C records the fast_math migration delta there: 10k sprite bounds dropped from about 3.64 ms to about 0.55 ms on the local Debug benchmark. Stage 10D adds a RelWithDebInfo Perf preset, release-like test assertion handling, dirty-transform scenarios, and large/huge local benchmark suites so later single-thread and ThreadCenter work has stable evidence. Stage 10E adds dirty-index Transform/Bounds updates and a zero-rotation transform fast path. Stage 10H adds `scripts/run_threaded_cpu_benchmarks.ps1` and `render2d_threaded_cpu_pipeline_bench`, which show ThreadCenter overhead on small high-visibility runs and benefit on 100k sprite runs.

Stage 10I also adds `render2d_upload_descriptor_compaction_bench`, a Perf benchmark for synthetic upload and descriptor stream compaction. The local 65,536-item run compacts each stream to 16,384 records and records average upload/descriptor compaction times in `BENCHMARK_BASELINE.md`.

## Current boundaries

Implemented:

- Strict POD component contracts
- Provider/Dim compile-time gates
- CPU systems through `CommandBuffer`
- Test-only temporary ECS storage
- Null CPU benchmark
- Native POD components
- Native runtime POD type/result contracts
- CPU-side Stage 7 native runtime skeletons for frame, device, queue, buffer, image, pipeline, descriptor, and swapchain records
- Stage 8A CPU-side encode/submit contract through `NativeCommandBufferRef`, `NativeCommandRuntime`, `EncodeSystem`, and `SubmitSystem`
- Stage 8B Vulkan command pool / command buffer lifecycle through `VulkanCommandRuntime`
- Stage 8C Vulkan sync and queue submit through `VulkanSyncRuntime` and `VulkanSubmitRuntime`
- Stage 8D Vulkan buffers/images, MemoryCenter allocation, upload/readback, copies, and layout transitions through `VulkanResourceRuntime`
- Stage 8E descriptors, shader modules, pipeline cache, and dynamic-rendering pipeline creation through `VulkanDescriptorRuntime` and `VulkanPipelineRuntime`
- Stage 8F MemoryCenter-backed persistent mapped upload ring with frame-slot reuse protection through `VulkanUploadRingRuntime`
- Stage 8G offscreen dynamic-rendering smoke through `VulkanDynamicRenderEncoder`
- Stage 9A Text/Glyph Strict POD components: `Utf8Slice`, `GlyphRun`, `GlyphInstance`, and `FontAtlasRef`
- Stage 9B deterministic test glyph systems through `GlyphRunBuildSystem`, `GlyphInstanceBuildSystem`, and `GlyphBuildConfig`
- Dormant FreeType source copied under `third_party/freetype` for future font integration; it is not built yet
- Stage 10C fast_math migration: `Render2D::Vec2`, `Render2D::Mat3`, and `Render2D::Aabb2` alias `MMath` POD types; custom Render2D `Aabb2` / `Affine2X3` structs are removed
- Stage 10D benchmark/profile harness: `clang-ninja-perf` preset, dirty transform benchmark input mutation, extended runner suites, and Stage 10 TODO tracking
- Stage 10E single-thread spatial hot path: `TransformDirtyItem`, `TransformSystem::runDirty`, `BoundsSystem::runDirty`, and zero-rotation transform fast path
- Stage 10F sort/batch foundation: packed draw sort keys, optional `DrawSortSystem`, and collision-safe packed-key-first `BatchSystem` comparison
- Stage 10G ThreadCenter integration: header-only runtime/system dependency embedded in CMake through `render2d_thread_runtime_support`, with smoke coverage and no ECS/public-interface contamination
- Stage 10H ThreadCenter-backed CPU pipeline runtime: deterministic chunked sprite path with single-thread equivalence coverage
- Stage 10I stream compaction: `UploadCoalesceSystem`, `DescriptorCompactionSystem`, tests, and benchmark coverage
- Stage 10J per-thread Vulkan command runtime: one command pool per runtime thread slot with runtime-only ownership metadata
- Stage 10K Stage 10 closeout: final checklist, ADR, project index, and verification documentation
- Stage 11A native frame/present POD contracts: `SwapchainImageRef`, `AcquiredImage`, `PresentCommand`, and `DeferredDestroyCommand`
- Stage 11D deferred destroy foundation: `NativeDeferredDestroyRuntime` queues retire commands and drains only frame-safe records

Not implemented yet:

- ThreadCenter-backed text pipeline work and parallel batch/sort tail stages
- swapchain creation, image acquire, present, and window-visible output
- production sprite instance shader/data layout
- real UTF-8 decoding, font shaping, glyph rasterization, and atlas packing
- production texture atlas / sampled-image descriptor policy
- Vulkan text draw integration
- RenderDoc automation; current capture target is the offscreen Vulkan smoke executable
