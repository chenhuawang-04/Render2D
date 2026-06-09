# Render2D Architecture

Render2D is a C++23, component-first, Vulkan-native rendering module. The current implementation keeps ECS-visible data as Strict POD streams while native runtimes own Vulkan object lifetimes behind `id + generation` references.

## Core principles

1. **ECS owns components.** All render data records are ECS components, including `VisibleItem`, `DrawCommand`, `BatchCommand`, `UploadCommand`, and `NativeSubmitCommand`.
2. **Components are Strict POD.** Components are trivial, standard-layout, trivially copyable, and aggregate. They do not own resources and do not contain `std::string`, `std::vector`, `std::span`, RAII wrappers, constructors, destructors, or virtual functions.
3. **Systems do not own ECS storage.** Production systems consume and write component streams through non-owning boundaries such as `std::span`.
4. **Provider/Dim are compile-time tags.** The current valid domain is `VulkanNativeProvider + Dim2`.
5. **Memory is centralized.** Render2D-owned dynamic CPU arrays use `Render2D::McVector` backed by MemoryCenter/Vector_New; Vulkan buffer/image backing memory is allocated and synchronized through `VulkanMemoryCenterAllocator`.
6. **Native runtime owns backend lifetimes.** ECS stores POD handles/IDs. Runtime tables own and validate backend slots behind those refs.

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

The CPU component pipeline remains ECS-driven. Stage 8 now attaches Vulkan command, sync, submit, resource, descriptor, pipeline, upload-ring, and dynamic-rendering encoder runtimes behind POD references. The offscreen smoke path records an indirect full-screen triangle draw and validates readback.

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

Implemented Vulkan-backed runtimes:

- `VulkanCommandRuntime` - Vulkan command pool and command buffer lifecycle owner behind `NativeCommandBufferRef`.
- `VulkanSyncRuntime` - real semaphore/fence lifecycle behind `FrameSync`.
- `VulkanSubmitRuntime` - real `vkQueueSubmit` using resolved command buffers and frame sync.
- `VulkanResourceRuntime` - real buffer/image/image-view lifecycle, MemoryCenter-backed GPU allocation, upload/readback, copies, and image layout tracking.
- `VulkanDescriptorRuntime` - descriptor pool, descriptor set layout, set allocation, and descriptor array updates.
- `VulkanPipelineRuntime` - shader module creation, pipeline cache, dynamic-rendering pipeline layout/pipeline creation.
- `VulkanUploadRingRuntime` - MemoryCenter-backed persistent mapped, frame-segmented upload ring; slices are not reusable until the frame is completed.
- `VulkanDynamicRenderEncoder` - records dynamic rendering, pipeline bind, direct draw, and indirect draw.

These runtime classes are not ECS storage. They own backend slot lifecycle metadata, validate generations, reject stale references, reuse slots, and keep Vulkan handles out of ECS components.

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
- Stage 8B Vulkan command pool / command buffer lifecycle through `VulkanCommandRuntime`
- Stage 8C Vulkan sync and queue submit through `VulkanSyncRuntime` and `VulkanSubmitRuntime`
- Stage 8D Vulkan buffers/images, MemoryCenter allocation, upload/readback, copies, and layout transitions through `VulkanResourceRuntime`
- Stage 8E descriptors, shader modules, pipeline cache, and dynamic-rendering pipeline creation through `VulkanDescriptorRuntime` and `VulkanPipelineRuntime`
- Stage 8F MemoryCenter-backed persistent mapped upload ring with frame-slot reuse protection through `VulkanUploadRingRuntime`
- Stage 8G offscreen dynamic-rendering smoke through `VulkanDynamicRenderEncoder`

Not implemented yet:

- deferred destroy queues
- swapchain creation, image acquire, present, and window-visible output
- production sprite instance shader/data layout
- production texture atlas / sampled-image descriptor policy
- RenderDoc automation; current capture target is the offscreen Vulkan smoke executable
