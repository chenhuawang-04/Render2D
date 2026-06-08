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
- `VulkanCommandRuntime<Provider, Dim>` - creates/destroys `VkCommandPool`, allocates/resolves/begins/ends/resets/releases `VkCommandBuffer` objects behind `NativeCommandBufferRef`.

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

## Non-goals in this stage

Current CPU-only native stages do not implement:

- Vulkan object creation or destruction
- descriptor pool/set allocation
- swapchain creation or image acquisition
- real fences, semaphores, or command pools
- draw command recording with `vkCmd*`
- MemoryCenter Vulkan allocation
- deferred destroy queues

The current runtime and encode/submit skeletons are CPU-only contracts that prepare the API boundary for later Vulkan-backed implementation.
