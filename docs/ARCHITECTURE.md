# Render2D Architecture

Render2D is a C++23, component-first, Vulkan-native rendering module. The current implementation intentionally focuses on data contracts, CPU-side systems, native POD references, and benchmarkable pipeline behavior before integrating real Vulkan runtime ownership.

## Core principles

1. **ECS owns components.** All render data records are ECS components, including derived records such as `VisibleItem`, `DrawCommand`, `BatchCommand`, `UploadCommand`, and `NativeSubmitCommand`.
2. **Components are Strict POD.** Components must be trivial, standard-layout, trivially copyable, and aggregate. They do not own resources and do not contain `std::string`, `std::vector`, `std::span`, RAII wrappers, constructors, destructors, or virtual functions.
3. **Systems do not own storage.** Production systems consume and write component streams through `std::span`. They do not depend on the temporary test ECS storage.
4. **Provider/Dim are compile-time tags.** Current valid domain is `VulkanNativeProvider + Dim2`.
5. **Native runtime owns backend objects.** ECS stores POD references such as `BufferRef` or `ImageRef`; future native runtime code owns actual Vulkan objects and lifetimes.

## Current data pipeline

```text
Transform[]
Sprite[]
Text[]
Camera[]
LocalBounds[]
VisibilityMask[]
    ->
TransformSystem
    ->
WorldTransform[]
    ->
BoundsSystem
    ->
WorldBounds[]
    ->
CullingSystem
    ->
VisibleItem[]
    ->
CommandBuildSystem
    ->
DrawCommand[]
    ->
BatchSystem
    ->
BatchCommand[]
    ->
CommandBufferBuildSystem
    ->
CommandBuffer[]
```

The current pipeline is CPU-only. It does not create Vulkan command buffers or GPU resources.

## Component layer

The component layer defines Strict POD ECS records:

- Scene/input components: `Transform`, `Sprite`, `Text`, `Camera`, `LocalBounds`, `VisibilityMask`, `RenderLayer`, `MaterialRef`, `TextureRef`, `FontRef`.
- Derived components: `WorldTransform`, `WorldBounds`, `VisibleItem`, `SortedItem`.
- Command components: `DrawCommand`, `BatchCommand`, `UploadCommand`, `NativeSubmitCommand`, `CommandBuffer`.
- Frame/native state components: `FrameIndex`, `FrameArenaState`, `DescriptorSlice`, `UploadRingSlice`, `FenceState`.
- Native resource references: `BufferRef`, `ImageRef`, `PipelineRef`, `DeviceHandle`, `QueueHandle`, `SwapchainState`, `FrameSync`, `UploadSlice`.

Every supported component is registered through `ComponentTraits` and checked by `SupportedRenderComponent`.

## System layer

Systems are stateless template types. They:

- take input component streams as `std::span<const T>`;
- take output component streams as `std::span<T>`;
- return `SystemResult`;
- do not allocate in the hot path;
- do not depend on ECS storage implementation;
- do not call Vulkan.

This keeps systems reusable when the temporary test ECS is replaced by the host engine ECS.

## Temporary test ECS

The repository includes test-only storage under:

```text
tests/support/
```

This storage exists only to validate components and systems. It is not production architecture and must be replaced by the host engine ECS during integration. This constraint is also tracked in `docs/ProjectMergeTODO.md`.

## Native runtime direction

Native references exposed to ECS use compact IDs and generation counters:

```text
resource_id + generation
```

Examples:

```text
BufferRef::buffer_id + BufferRef::generation
ImageRef::image_id + ImageRef::generation
PipelineRef::pipeline_id + PipelineRef::generation
```

The future Native Runtime will use those IDs to resolve into backend-owned slot tables. Generation checks prevent stale ECS references from accidentally resolving to reused native resources.

Current native runtime work has only defined type contracts:

- `NativeStatusCode`
- `NativeObjectKind`
- `NativeMemoryDomain`
- `NativeHandle`
- `NativeId`
- `NativeGeneration`
- `NativeResourceKey`
- `NativeByteRange`
- `NativeResult`
- `NativeCapacityResult`

The first CPU-side native runtime skeleton is also implemented:

- `NativeResourceRuntime<Provider, Dim>`

It manages `BufferRef` and `ImageRef` slot tables with id + generation validation, release, and slot reuse. It does not call Vulkan and does not own real GPU objects yet.

No Vulkan API calls or real GPU resource ownership are implemented yet.

## Benchmarking

The Null CPU benchmark validates the current pipeline without Vulkan:

```powershell
cmake --preset clang-ninja-debug -DRENDER2D_BUILD_BENCHMARKS=ON
cmake --build build
.\build\bench\render2d_null_cpu_bench.exe --sprites 10000 --frames 4
```

It reports visible count, draw count, batch count, and average pass timings.

## Current boundaries

Implemented:

- Strict POD component contracts
- Provider/Dim compile-time gates
- CPU systems through `CommandBuffer`
- Test-only temporary ECS storage
- Null CPU benchmark
- Native POD components
- Native runtime POD type/result contracts
- CPU-side `NativeResourceRuntime` slot table skeleton for `BufferRef` / `ImageRef`

Not implemented yet:

- complete Vulkan-backed native resource tables
- deferred destroy queues
- MemoryCenter-backed native allocation
- Vulkan object creation/destruction
- descriptor runtime
- upload runtime
- command runtime
- swapchain runtime
- Vulkan encoder and submit systems
