# Render2D Architecture

Render2D is a C++23, component-first, Vulkan-native rendering module. The current implementation focuses on strict data contracts, CPU-side systems, native POD references, and benchmarkable pipeline behavior before integrating real Vulkan object creation.

## Core principles

1. **ECS owns components.** All render data records are ECS components, including `VisibleItem`, `DrawCommand`, `BatchCommand`, `UploadCommand`, and `NativeSubmitCommand`.
2. **Components are Strict POD.** Components are trivial, standard-layout, trivially copyable, and aggregate. They do not own resources and do not contain `std::string`, `std::vector`, `std::span`, RAII wrappers, constructors, destructors, or virtual functions.
3. **Systems do not own ECS storage.** Production systems consume and write component streams through non-owning boundaries such as `std::span`.
4. **Provider/Dim are compile-time tags.** The current valid domain is `VulkanNativeProvider + Dim2`.
5. **Native runtime owns backend lifetimes.** ECS stores POD handles/IDs. Runtime tables own and validate backend slots behind those refs.

## Current data pipeline

```text
Transform[] / Sprite[] / Text[] / Camera[]
    -> TransformSystem
    -> BoundsSystem
    -> CullingSystem
    -> CommandBuildSystem
    -> BatchSystem
    -> CommandBufferBuildSystem
    -> CommandBuffer[]
    -> EncodeSystem
    -> NativeCommandBufferRef[]
    -> SubmitSystem
    -> NativeSubmitCommand[]
```

The current pipeline is CPU-only. It builds encode/submit descriptors but does not call Vulkan or create GPU objects.

## Component layer

The component layer defines Strict POD ECS records:

- Scene/input components: `Transform`, `Sprite`, `Text`, `Camera`, `LocalBounds`, `VisibilityMask`, `RenderLayer`, `MaterialRef`, `TextureRef`, `FontRef`.
- Derived components: `WorldTransform`, `WorldBounds`, `VisibleItem`, `SortedItem`.
- Command components: `DrawCommand`, `BatchCommand`, `UploadCommand`, `NativeSubmitCommand`, `CommandBuffer`.
- Frame/native state components: `FrameIndex`, `FrameArenaState`, `DescriptorSlice`, `UploadRingSlice`, `FenceState`.
- Native resource references: `DeviceHandle`, `QueueHandle`, `SwapchainState`, `FrameSync`, `NativeCommandBufferRef`, `PipelineRef`, `ImageRef`, `BufferRef`, `UploadSlice`.

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

The repository includes test-only storage under `tests/support/`. This storage exists only to validate components and systems. It is not production architecture and must be replaced by the host engine ECS during integration.

## Native runtime skeleton

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
```

Implemented CPU-side runtime skeletons:

- `NativeFrameRuntime` - frame-in-flight slot rotation and `FrameSync` output.
- `NativeDeviceRuntime` - device/queue handle slot tables.
- `NativeResourceRuntime` - buffer/image reference slot tables.
- `NativePipelineRuntime` - pipeline reference slot table.
- `NativeDescriptorRuntime` - descriptor slice slot table.
- `NativeSwapchainRuntime` - swapchain state slot table and resize generation bump.
- `NativeCommandRuntime` - native command buffer reference slot table.

These runtime classes are not ECS storage. They own backend slot lifecycle metadata, validate generations, reject stale references, and reuse slots. They still do not call Vulkan and do not allocate real GPU resources.

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
- CPU-side Stage 7 native runtime skeletons for frame, device, queue, buffer, image, pipeline, descriptor, and swapchain records
- Stage 8A CPU-side encode/submit contract through `NativeCommandBufferRef`, `NativeCommandRuntime`, `EncodeSystem`, and `SubmitSystem`

Not implemented yet:

- Vulkan object creation/destruction
- MemoryCenter-backed Vulkan allocation
- deferred destroy queues
- real descriptor pool/set allocation
- upload ring implementation
- real Vulkan command buffer allocation/recording
- real Vulkan queue submit/present
