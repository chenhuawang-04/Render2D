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

Reason:

- ECS components may outlive the native resource slot they reference.
- `generation` lets runtime code reject stale references after slot reuse.
- IDs remain compact and directly indexable by native runtime tables.

Native runtime owns actual Vulkan objects later. ECS components only store POD references.

## 3. Native runtime is not ECS storage

Stage 7 runtime classes are backend runtime owner-table skeletons, not an ECS component manager.

Current runtime classes:

- `NativeFrameRuntime`
- `NativeDeviceRuntime`
- `NativeResourceRuntime`
- `NativePipelineRuntime`
- `NativeDescriptorRuntime`
- `NativeSwapchainRuntime`

During host-engine merge:

- keep ECS ownership of POD components in the host ECS;
- keep native slot lifetime, generation validation, and stale-reference rejection in Render2D native runtime;
- do not replace native runtime tables with scene ECS storage.

## 4. Stage 7 remains CPU-only

The current native runtime skeleton does not call Vulkan. It only validates the component/runtime boundary and slot lifecycle semantics.

Later Vulkan stages must attach real handles, MemoryCenter allocations, descriptor pools, swapchain creation, sync objects, and deferred destruction behind the existing `id + generation` contracts.
