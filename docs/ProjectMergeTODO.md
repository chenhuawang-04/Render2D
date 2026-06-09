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

Stage 8B and later attach real Vulkan objects behind the same POD references. MemoryCenter-backed Vulkan allocation is now implemented for resources/upload; swapchain creation/present and deferred destruction are still host-merge concerns.


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


## 10. MemoryCenter and McVector are mandatory for Render2D-owned storage

Render2D-owned dynamic arrays must use `Render2D::McVector<T>`, which aliases Vector_New `Center::Memory::mc_vector` and routes allocation through MemoryCenter. Do not introduce `std::vector` in Render2D source, test support, or benchmarks.

Vulkan buffer/image backing memory must be allocated, persistently mapped, flushed, invalidated, and freed through `VulkanMemoryCenterAllocator`. Render2D runtime code should not call `vkAllocateMemory`, `vkFreeMemory`, `vkMapMemory`, `vkUnmapMemory`, `vkBindBufferMemory`, or `vkBindImageMemory` directly.

Merge rule: host ECS owns only POD component streams and refs; Render2D native runtime owns MemoryCenter-backed CPU runtime tables and GPU allocation slices behind `id + generation` references.


## 11. Text/Glyph data is POD-only in Stage 9A

Stage 9A adds only data contracts: `Utf8Slice`, `GlyphRun`, `GlyphInstance`, and `FontAtlasRef` are Strict POD ECS components.

Merge rule:

- host ECS owns the component streams;
- UTF-8 backing buffers, font files, shaping caches, atlas images, and glyph raster data remain outside ECS components;
- glyph systems should pass only ids, ranges, atlas rects, positions, colors, sort keys, and flags through ECS.

Stage 9 is now complete for the dependency-free ECS pipeline: `TextDirtySystem` emits `TextDirtyRange[]`, dirty glyph run/instance update paths rebuild only changed ranges, and `GlyphBatchSystem` emits regular `DrawCommand[]` over `GlyphInstance[]`. Still not implemented: real UTF-8 decoding, font shaping, atlas packing, sampled-image descriptor policy, and production Vulkan text shader/descriptor integration.


## 12. FreeType is vendored but dormant

FreeType source has been copied to `third_party/freetype/` from `E:/Project/MelosyneTest/VulkanRender_New/freetype-master`.

Current rule:

- do not include FreeType headers from Render2D public headers;
- do not add FreeType to CMake until a dedicated font runtime stage starts;
- keep Stage 9 glyph systems deterministic and dependency-free until a dedicated font runtime stage;
- when FreeType is enabled later, keep all FreeType handles, faces, glyph slots, and atlas ownership outside ECS components.

License note: FreeType offers FTL or GPLv2+ choices; choose and document the project policy before shipping or linking it.

## 13. Stage 9 text pipeline remains dependency-free

Stage 9 intentionally does not link FreeType. Its purpose is to lock the ECS-facing text pipeline shape:

- host ECS owns `Text`, `TextState`, `TextDirtyRange`, `GlyphRun`, `GlyphInstance`, and generated `DrawCommand` streams;
- unchanged `Text` entries can skip glyph run/instance rebuild through dirty ranges;
- `GlyphBatchSystem` converts glyph runs into the same draw-command path used by sprites;
- future FreeType/shaping/atlas code must update backing font runtime data outside ECS components.

## 14. Render math is fast_math POD-only

Render2D no longer owns independent math structs for 2D bounds or affine transforms.

Current merge rule:

- `Render2D::Vec2` is `MMath::Vec2`;
- `Render2D::Mat3` is `MMath::Mat3`;
- `Render2D::Aabb2` is `MMath::Aabb2`;
- `WorldTransform::affine` stores `Mat3`, not a custom `Affine2X3`;
- AABB construction/access must go through `makeAabb2`, `aabb2Min`, and `aabb2Max` because fast_math stores max values internally as negatives.

Host-engine integration should map its ECS component streams to these POD math fields directly or through a single conversion edge. Do not reintroduce Render2D-local vector, matrix, or AABB implementations.
