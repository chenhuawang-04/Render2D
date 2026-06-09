# ProjectMergeTODO

This document tracks decisions and cleanup items that matter when Render2D is merged into the host engine.

## 1. ECS framework exists only for tests

The temporary ECS framework in this repository exists only under `tests/support/`.

It is used to validate Render2D components and systems before integration. It is not the production ECS and should be replaced by the host engine ECS during merge.

Relevant temporary files:

- `tests/support/TemporaryEcsStorage.hpp`
- `tests/support/ComponentStreamView.hpp`

Production systems should continue to consume component streams through non-owning boundaries such as `std::span`, not through the temporary test ECS storage.

## 2. Resource references exposed to ECS use id + generation

Render2D native resource references exposed to ECS use an `id + generation` pattern.

Examples:

- `DeviceHandle::device_id + DeviceHandle::generation`
- `QueueHandle::queue_id + QueueHandle::generation`
- `SwapchainState::swapchain_id + SwapchainState::generation`
- `BufferRef::buffer_id + BufferRef::generation`
- `ImageRef::image_id + ImageRef::generation`
- `PipelineRef::pipeline_id + PipelineRef::generation`
- `DescriptorSlice::descriptor_set_id + DescriptorSlice::generation`
- `NativeCommandBufferRef::command_buffer_id + NativeCommandBufferRef::generation`

Reason:

- ECS components may outlive the native resource slot they reference.
- `generation` lets runtime code reject stale references after slot reuse.
- IDs remain compact and directly indexable by native runtime tables.

Native runtime owns actual Vulkan objects later. ECS components only store POD references.

## 3. Native runtime is not ECS storage

Stage 7/8A runtime classes are backend runtime owner-table skeletons, not an ECS component manager.

Current runtime classes:

- `NativeFrameRuntime`
- `NativeDeviceRuntime`
- `NativeResourceRuntime`
- `NativePipelineRuntime`
- `NativeDescriptorRuntime`
- `NativeSwapchainRuntime`
- `NativeCommandRuntime`
- `VulkanCommandRuntime`

During host-engine merge:

- keep ECS ownership of POD components in the host ECS;
- keep native slot lifetime, generation validation, and stale-reference rejection in Render2D native runtime;
- do not replace native runtime tables with scene ECS storage.

## 4. Native runtime stages before Stage 8B remain CPU-only

The CPU-side native runtime and encode/submit skeletons do not call Vulkan. They validate the component/runtime boundary and slot lifecycle semantics.

Stage 8B and later attach real Vulkan objects behind the same POD references. MemoryCenter-backed Vulkan allocation, swapchain creation/present, and deferred destruction are still host-merge concerns.


## 5. Stage 8A is an encode/submit contract only

`EncodeSystem` and `SubmitSystem` currently produce POD descriptors only:

- `CommandBuffer` plus batch/upload ranges -> `NativeCommandBufferRef`
- `NativeCommandBufferRef[]` plus `FrameSync` -> `NativeSubmitCommand`

They do not call Vulkan and should remain replaceable by the host engine ECS stream wiring during merge.


## 6. Stage 8B owns Vulkan command objects only

`VulkanCommandRuntime` owns real `VkCommandPool` and `VkCommandBuffer` lifetimes. These Vulkan handles must not move into ECS components.

During host-engine merge:

- keep `NativeCommandBufferRef` in the host ECS;
- keep `VkCommandPool` / `VkCommandBuffer` ownership in Render2D native runtime;
- wire the host ECS streams into `EncodeSystem`, `SubmitSystem`, and later Vulkan recording systems.

## 7. Stage 8C-G Vulkan runtimes own native handles, ECS owns POD refs

The completed Stage 8 Vulkan runtimes now own real native resources:

- `VulkanSyncRuntime` - semaphores and fences
- `VulkanSubmitRuntime` - queue submit
- `VulkanResourceRuntime` - buffers, images, image views, memory, upload/readback helpers
- `VulkanDescriptorRuntime` - descriptor pool, descriptor set layout, descriptor sets
- `VulkanPipelineRuntime` - shader modules, pipeline cache, pipeline layouts, pipelines
- `VulkanUploadRingRuntime` - persistent mapped upload buffer and per-frame ring slots
- `VulkanDynamicRenderEncoder` - dynamic rendering and direct/indirect draw recording

During merge, keep these Vulkan handles in native runtime. The host ECS should store only `FrameSync`, `BufferRef`, `ImageRef`, `PipelineRef`, `DescriptorSlice`, `UploadRingSlice`, and `NativeCommandBufferRef`.

## 8. Upload ring frame slots must not be reused early

`VulkanUploadRingRuntime` divides the mapped ring into frame segments. A segment becomes reusable only after `completeFrame(frame_index)` is called, which should happen after the matching GPU fence has completed.

Merge rule:

- call `beginFrame(frame_index)` before allocating upload slices;
- allocate/write `UploadRingSlice` records for that frame;
- submit GPU work;
- wait or otherwise prove the matching fence is complete;
- call `completeFrame(frame_index)`.

Old `UploadRingSlice` records become stale by generation after completion.

## 9. Current visible proof is offscreen, not swapchain-presented

The Stage 8 smoke test renders a magenta sprite-like full-screen triangle into an offscreen `R8G8B8A8` image and verifies readback bytes. This proves command recording, dynamic rendering, indirect draw, upload ring, queue submit, and resource readback without requiring a platform window.

Host-engine merge still needs swapchain/window integration for on-screen presentation and RenderDoc capture of a real window frame.
