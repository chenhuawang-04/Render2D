# Native Runtime Contract

Native Runtime owns backend slot lifetimes. ECS owns only POD references such as `DeviceHandle`, `BufferRef`, `ImageRef`, `PipelineRef`, `DescriptorSlice`, and `SwapchainState`.

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
```

## Stage 7 CPU-side runtime skeletons

Implemented runtime skeletons:

- `NativeFrameRuntime<Provider, Dim>` - configures frame-in-flight count, emits `FrameIndex` and `FrameSync`, and rotates frame slots.
- `NativeDeviceRuntime<Provider, Dim>` - reserves, creates, resolves, releases, and reuses `DeviceHandle` / `QueueHandle` slots.
- `NativeResourceRuntime<Provider, Dim>` - reserves, creates, resolves, releases, and reuses `BufferRef` / `ImageRef` slots.
- `NativePipelineRuntime<Provider, Dim>` - reserves, creates, resolves, releases, and reuses `PipelineRef` slots.
- `NativeDescriptorRuntime<Provider, Dim>` - reserves, allocates, resolves, releases, and reuses `DescriptorSlice` slots.
- `NativeSwapchainRuntime<Provider, Dim>` - reserves, creates, resolves, resizes, releases, and reuses `SwapchainState` slots.

All slot-backed runtimes validate generation values and return `NativeStatusCode::StaleReference` for stale ECS references.

## Non-goals in this stage

Stage 7 does not implement:

- Vulkan object creation or destruction
- descriptor pool/set allocation
- swapchain creation or image acquisition
- fences, semaphores, or command pools
- MemoryCenter Vulkan allocation
- deferred destroy queues

The current runtime skeletons are CPU-only contracts that prepare the API boundary for later Vulkan-backed implementation.
