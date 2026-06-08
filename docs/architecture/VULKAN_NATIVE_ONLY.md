# Vulkan Native Only

Render2D currently supports only `VulkanNativeProvider + Dim2`. The Provider layer does not abstract OpenGL, D3D, Metal, or a generic backend.

## Stage 7A: Native POD ECS Components

Stage 7A defines Vulkan-native state as Strict POD ECS components only. These components store IDs, indices, generation values, integer handle values, ranges, flags, and sizes.

Implemented components:

```cpp
DeviceHandle<Provider, Dim>
QueueHandle<Provider, Dim>
SwapchainState<Provider, Dim>
FrameSync<Provider, Dim>
PipelineRef<Provider, Dim>
ImageRef<Provider, Dim>
BufferRef<Provider, Dim>
UploadSlice<Provider, Dim>
```

Already-existing native/frame state components remain valid:

```cpp
DescriptorSlice<Provider, Dim>
UploadRingSlice<Provider, Dim>
FenceState<Provider, Dim>
```

## Ownership Rule

Native components do not own Vulkan resources. A handle field is only a value record. Components must not call `vkDestroy*`, allocate memory, hold RAII wrappers, or manage synchronization lifetimes.

Resource creation, destruction, reuse, descriptor allocation, upload rings, swapchain recreation, and frame-in-flight synchronization belong to later native runtime/storage work.

## Not Implemented in 7A

Stage 7A intentionally does not implement:

- `VkBuffer` creation
- `VkImage` creation
- `VkPipeline` creation
- descriptor allocation
- swapchain creation
- command pools
- Vulkan validation tests
- MemoryCenter Vulkan allocation integration
