# Native Runtime Contract

Native Runtime owns backend slot lifetimes. ECS owns only POD references such as `DeviceHandle`, `BufferRef`, `ImageRef`, `PipelineRef`, `DescriptorSlice`, `SwapchainState`, and `NativeCommandBufferRef`.

## Type contracts

Native runtime APIs return POD result values and do not throw exceptions across the runtime boundary.

Implemented files:

```text
include/Render2D/Native/NativeTypes.hpp
include/Render2D/Native/NativeResult.hpp
```

Implemented POD contracts:

```cpp
NativeStatusCode
NativeObjectKind
NativeMemoryDomain
NativeHandle
NativeId
NativeGeneration
NativeResourceKey
NativeByteRange
NativeResult
NativeCapacityResult
```

## id + generation

Native references exposed to ECS use compact IDs plus generation counters. Runtime tables use IDs as direct indices and generations to reject stale references after release, resize, or slot reuse.

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

## CPU-side runtime skeletons

Implemented runtime skeletons:

- `NativeFrameRuntime<Provider, Dim>` - configures frame-in-flight count, emits `FrameIndex` and `FrameSync`, and rotates frame slots.
- `NativeDeviceRuntime<Provider, Dim>` - reserves, creates, resolves, releases, and reuses `DeviceHandle` / `QueueHandle` slots.
- `NativeResourceRuntime<Provider, Dim>` - reserves, creates, resolves, releases, and reuses `BufferRef` / `ImageRef` slots.
- `NativePipelineRuntime<Provider, Dim>` - reserves, creates, resolves, releases, and reuses `PipelineRef` slots.
- `NativeDescriptorRuntime<Provider, Dim>` - reserves, allocates, resolves, releases, and reuses `DescriptorSlice` slots.
- `NativeSwapchainRuntime<Provider, Dim>` - reserves, creates, resolves, resizes, releases, and reuses `SwapchainState` slots.
- `NativeCommandRuntime<Provider, Dim>` - reserves, creates, resolves, releases, and reuses CPU-only `NativeCommandBufferRef` slots.

All slot-backed runtimes validate generation values and return `NativeStatusCode::StaleReference` for stale ECS references.

## Stage 8A CPU-side encode/submit contract

Implemented system contracts:

- `EncodeSystem<Provider, Dim>` validates `CommandBuffer` batch/upload ranges and allocates a `NativeCommandBufferRef` through `NativeCommandRuntime`.
- `SubmitSystem<Provider, Dim>` converts `NativeCommandBufferRef[]` plus `FrameSync` into `NativeSubmitCommand[]`.

This stage only produces ECS-visible POD descriptors. It does not call `vkBeginCommandBuffer`, `vkCmd*`, or `vkQueueSubmit`.

## Stage 8B Vulkan command lifecycle

`VulkanCommandRuntime<Provider, Dim>` owns the first real Vulkan runtime lifecycle in Render2D:

- `vkCreateCommandPool` / `vkDestroyCommandPool`
- `vkAllocateCommandBuffers` / `vkFreeCommandBuffers`
- `vkBeginCommandBuffer` / `vkEndCommandBuffer`
- `vkResetCommandBuffer` / `vkResetCommandPool`

It still exposes only `NativeCommandBufferRef` to ECS. Real `VkCommandBuffer` handles stay inside the native runtime.

## Stage 10J per-thread Vulkan command lifecycle

`VulkanThreadCommandRuntime<Provider, Dim>` adds the parallel-recording ownership model:

- one `VkCommandPool` per runtime thread slot;
- command buffers allocated from the caller-selected thread pool;
- per-thread pool reset through `resetThreadCommandPool`;
- full runtime reset through `resetAllCommandPools`;
- stale `NativeCommandBufferRef` rejection and generation reuse semantics matching the single-pool runtime.

Thread ownership is runtime metadata only. `NativeCommandBufferRef` remains the only ECS-visible command-buffer record.

## Stage 8C Vulkan sync and submit

Implemented Vulkan sync/submit contracts:

- `VulkanSyncRuntime<Provider, Dim>` owns `VkSemaphore` and `VkFence` objects behind `FrameSync`.
- `VulkanSubmitRuntime<Provider, Dim>` resolves `NativeCommandBufferRef[]` and `FrameSync`, then calls `vkQueueSubmit`.

`FrameSync` now carries `sync_id + generation` so stale frame sync records are rejected after release/reuse.

## Stage 8D Vulkan resources and upload/readback

`VulkanResourceRuntime<Provider, Dim>` owns real resources behind POD refs:

- `VkBuffer` + MemoryCenter Vulkan allocation slice behind `BufferRef`
- `VkImage` + `VkImageView` + MemoryCenter Vulkan allocation slice behind `ImageRef`
- host-visible upload/readback buffer writes and reads
- device-local buffers/images
- buffer copy, buffer barriers, image layout transitions, and image-to-buffer readback

ECS still sees only `BufferRef` and `ImageRef`.

## Stage 8E descriptors and pipelines

Implemented Vulkan descriptor/pipeline contracts:

- `VulkanDescriptorRuntime<Provider, Dim>` owns descriptor pool, descriptor set layout, descriptor sets, and descriptor array updates.
- `VulkanPipelineRuntime<Provider, Dim>` owns pipeline cache, pipeline layouts, graphics pipelines, and shader module creation/destruction.

`DescriptorSlice` and `PipelineRef` remain POD records with generation validation.

## Stage 8F upload ring lifetime

`VulkanUploadRingRuntime<Provider, Dim>` owns a MemoryCenter-backed persistent mapped upload buffer split into frame segments. It emits `UploadRingSlice` records with `ring_id + generation`.

Safety rule:

```text
beginFrame -> allocate/write UploadRingSlice -> submit -> fence complete -> completeFrame
```

A frame segment cannot be reused before `completeFrame`, and old slices become stale after completion.

## Stage 8G dynamic rendering encoder

`VulkanDynamicRenderEncoder<Provider, Dim>` records Vulkan draw commands:

- transitions an offscreen color target to `COLOR_ATTACHMENT_OPTIMAL`;
- begins dynamic rendering;
- sets viewport/scissor;
- binds the dynamic-rendering pipeline;
- records direct `vkCmdDraw` or indirect `vkCmdDrawIndirect`;
- ends dynamic rendering.

The smoke test uses the upload ring to hold `VkDrawIndirectCommand`, renders a magenta full-screen sprite-like triangle into an offscreen `R8G8B8A8` image, copies it to a readback buffer, and verifies the bytes.

## Stage 11A/11D frame-present records and deferred destroy

Stage 11 introduces ECS-visible POD records for the future window-visible frame loop:

- `SwapchainImageRef`
- `AcquiredImage`
- `PresentCommand`
- `DeferredDestroyCommand`

These records carry IDs, generations, frame indices, sync ids, flags, and non-owning handle snapshots only. They do not own Vulkan lifetimes.

`NativeDeferredDestroyRuntime<Provider, Dim>` owns a runtime queue of `DeferredDestroyCommand` records in `McVector`. It validates commands, applies an optional safe frame lag, preserves FIFO ordering among drained commands, and refuses to mutate the queue when the caller-provided output span is too small. The queue decides when a record is safe to release; the actual Vulkan destruction still happens through the native runtime that owns the resource slot.

## Stage 11B Vulkan swapchain runtime

`VulkanSwapchainRuntime<Provider, Dim>` provides the swapchain boundary for host-engine integration:

- host code provides `VkSurfaceKHR`;
- Render2D can create a `VkSwapchainKHR` from that surface;
- Render2D can also adopt a host-created `VkSwapchainKHR`;
- the runtime queries swapchain images and creates image views;
- ECS-visible state is `SwapchainState` and `SwapchainImageRef`;
- stale swapchain/image refs are rejected through generation validation.

The runtime owns created image views and only destroys the swapchain handle when it was created by Render2D or explicitly marked as owned. Swapchain image memory is not allocated through `VulkanMemoryCenterAllocator` because Vulkan owns swapchain image memory internally.

## Stage 11C Vulkan acquire and present

`VulkanPresentRuntime<Provider, Dim>` connects swapchain state and frame sync to Vulkan presentation:

- `acquireNextImage` resolves `SwapchainState` and `FrameSync`, then calls `vkAcquireNextImageKHR`;
- acquire emits `AcquiredImage`;
- `present` resolves `PresentCommand` and `FrameSync`, then calls `vkQueuePresentKHR`;
- Vulkan acquire/present results are mapped to `NativeStatusCode`;
- stale swapchain and stale sync records are rejected through runtime generation checks.

`AcquiredImage` and `PresentCommand` store sync generations. They still store no Vulkan semaphore or queue handles.

## Stage 11E acquire/present state flow

`PresentCommandBuildSystem<Provider, Dim>` is the ECS-side bridge from acquisition to presentation:

- input: `AcquiredImage[]`;
- output: `PresentCommand[]`;
- behavior: copy swapchain id, image index, frame index, swapchain generation, sync id, sync generation, and flags;
- invalid state: zero swapchain generation or zero sync generation.

The system is a pure component-stream transform and does not allocate or retain ECS storage.

`mapVulkanAcquirePresentResult` classifies `VK_ERROR_OUT_OF_DATE_KHR` and `VK_ERROR_SURFACE_LOST_KHR` as `NativeStatusCode::SwapchainOutOfDate`. `VulkanPresentRuntime::acquireNextImage` and `VulkanPresentRuntime::present` resolve the complete `SwapchainState` and reject out-of-range image indices as invalid input.

The automated stage remains host-window-free. Window-visible capture must be supplied by the host engine because Render2D deliberately does not own windows or `VkSurfaceKHR`.

## Runtime memory policy

Render2D runtime-owned dynamic arrays use `Render2D::McVector<T>` instead of `std::vector`. Vulkan resource and upload-ring backing memory is owned through `VulkanMemoryCenterAllocator`, which wraps MemoryCenter Vulkan adaptors and centralizes allocation, binding, persistent mapping, flushing, invalidation, and deallocation.

ECS-visible refs remain Strict POD and never store MemoryCenter allocators or allocation objects.

## Remaining non-goals

The current implementation still does not implement:

- host-window surface integration and window-visible present smoke
- production sprite instance shader/data layout
- production texture atlas and sampled-image policy

The current offscreen smoke path is intentionally windowless so it can run as a deterministic CTest target.
