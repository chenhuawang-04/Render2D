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

## 15. ThreadCenter is runtime infrastructure only

ThreadCenter source is expected at `E:/Project/MelosyneTest/ThreadCenter` for Stage 10G and later.

Merge rule:

- use ThreadCenter only in Render2D runtime/system orchestration;
- do not store ThreadCenter types in ECS components;
- keep single-thread systems as the deterministic correctness reference;
- multi-thread paths must write per-thread streams and merge deterministically;
- host ECS still owns component streams.

Stage 10G integration note:

- Render2D embeds ThreadCenter in CMake with header API only;
- current integration target is `render2d_thread_runtime_support`;
- `render2d_thread_runtime_support` links `Render2D::Render2D` + `Center.Thread.Headers`;
- public `Render2D::Render2D` remains ThreadCenter-free until a real public runtime header needs it.

Stage 10H integration note:

- `ThreadedCpuPipelineRuntime` is runtime orchestration, not ECS storage;
- host ECS still passes ordinary component streams through `std::span`;
- the fused spatial front-end (`SpatialCullSystem`, Stage 24 Track 1) writes `WorldTransform` by deterministic chunk and per-chunk visible-item scratch, merged in chunk order (`WorldBounds` is no longer materialized — see the Stage 24 note below);
- CommandBuild writes fixed output slots by deterministic chunk;
- Batch remains the single-thread reference tail stage until a dedicated parallel batch design lands.

Stage 21B integration note:

- `ThreadedDrawSortRuntime` (the dedicated deterministic parallel sort anticipated above) parallelizes only the radix sort; its output is byte-identical to `DrawSortSystem::run` (bucket-major-then-chunk-order scatter offsets reproduce the serial stable permutation — see ADR `2026-06-15-stage21-parallel-deterministic-draw-sort.md`);
- `ThreadedBatchRuntime` (added after the at-scale sweep showed batch is `5–15 ms` at 1–2M draws, not the `~0.02 ms` of the 10k reading) parallelizes the batch merge; its output is byte-identical to `BatchSystem::run` / `runBindless` via a segmented start-based scan, threshold-gated, with a full-capacity contract (batch stream ≥ draw count) — see ADR `2026-06-15-stage21-parallel-deterministic-batch.md`. `BatchSystem` stays the single-thread reference and sub-threshold path;
- both runtimes consume/produce `std::span`, own only `McVector` scratch, and are threshold-gated via `ParallelPolicy.hpp` (#22);
- they own separate ThreadCenter executors; a host that runs the sorted tail (`sort → batch`) may want to share one executor across both — that consolidation is a host orchestration choice, deliberately not made in this repo.

Stage 21 close note: the remaining (optional, data-driven) 21C/21D were decided on at-scale data, not added. SIMD bounds/culling (21C) is declined — those stages are memory-bandwidth-bound (whole-pipeline threading plateaus at ~1.3x; 20 workers ≈ 4), so SIMD has no win. The real bottleneck is the rotating `TransformSystem` (trig), which is compute-bound: its SIMD `sincos` belongs in **fast_math** (the math invariant forbids Render2D-local trig) and a SoA transform layout (21D) is the **host ECS's** storage choice — both out of this repo's scope. The actionable follow-up is a fast_math batched-`sincos` request, not a Render2D change.

Stage 24 Track 1 note (fused spatial front-end): `SpatialCullSystem` fuses `transform → bounds → culling` into one pass and is now the runtime's front-end (and its single-thread reference). Merge implications:

- `WorldTransform` is still produced (downstream `SpriteInstanceBuildSystem` reads the affine); `WorldBounds` is **no longer written** by `runSpritePipeline` — its span is retained in the signature and capacity-validated but ignored. A host that wants a materialized `WorldBounds` stream must call `BoundsSystem` itself; nothing in this repo's pipeline consumes it past culling.
- The fused output (`WorldTransform`/`VisibleItem`/draws/batches) is byte-identical to the `TransformSystem → BoundsSystem → CullingSystem` chain; the three granular systems remain the deterministic reference.
- Win is bandwidth-bound-case only (`1.5–2.2x` static); the rotating/compute case is neutral (`~1.0x`) and still waits on the fast_math batched-`sincos` follow-up above (Stage 24 Track 2).

Stage 24 Track 2 note (rotating transform / AVX2+FMA): the fast_math `sincos` was unified so scalar and SIMD agree bit-for-bit on FMA targets and the kernel is FMA-conditional (`Melosyne-Math` `83b1977`). Merge implications:

- The rotating-transform lever is **hardware FMA**, not explicit batching: with `-mfma` the existing `TransformSystem` goes ~50 ms → ~7 ms (~7x) at 1M because `std::fma` becomes one instruction. The explicit `BatchedTransformSystem` is bandwidth-bound and not a clear win (~0.9x FMA / ~1.26x non-FMA); it is kept as a non-FMA option and as the `mat3FromTrsArray` contract test, but `TransformSystem` stays the default path.
- **Build-contract knob:** `RENDER2D_ENABLE_AVX2` (OFF by default; the `clang-ninja-perf` preset sets it ON) adds `-mavx2 -mfma` to the `Render2D` INTERFACE target, raising the minimum CPU for AVX2 builds to **x86-64-v3 (Haswell, 2013+)**. A host engine merging Render2D decides its own target ISA: leave it OFF for baseline portability (math still correct, just no FMA speedup) or set it ON to unlock the ~7x and the 8-wide `mat3FromTrsArray`. See ADR `2026-06-16-render2d-avx2-fma-build-option.md`.
- `BatchedTransformSystem` is byte-identical to a per-element `MMath::sincos` build (deterministic, chunk-invariant) and differs from `TransformSystem::run` only by the sign of zero in `m01` for `rotation==0` (numerically equal). A host wiring it into a threaded driver gets bit-for-bit chunk merging on FMA targets.

## 16. TransformDirtyItem is an ECS-visible component

Stage 10E adds `TransformDirtyItem<Provider, Dim>` as a Strict POD dirty-index component.

Merge rule:

- host ECS owns and fills dirty transform item streams;
- each item addresses a transform/world-bounds slot by `source_index`;
- Render2D dirty systems consume this stream but do not own ECS storage;
- `TransformSystem::run` and `BoundsSystem::run` remain full-stream correctness references;
- `runDirty` is only valid after a full initialization pass has populated `WorldTransform[]` and `WorldBounds[]`.

## 17. Draw sort key is the batch/sort contract

Stage 10F uses `DrawCommand.sort_key` as a packed sort key generated by `makeDrawSortKey(layer, material_id, texture_id, flags)`.

Merge rule:

- host ECS may pre-sort or leave draw streams unsorted;
- `DrawSortSystem` is optional and uses caller-owned scratch component streams;
- sorting is explicit because it reduces batch count but costs CPU time;
- `BatchSystem` first compares packed `sort_key`, then verifies material, texture, layer, and flags to prevent collision merges;
- future host integration should generate the same key layout before handing `DrawCommand[]` to Render2D systems.

## 18. Upload and descriptor compaction still outputs ECS components

Stage 10I adds:

- `UploadCoalesceSystem`
- `DescriptorCompactionSystem`

Merge rule:

- host ECS owns the input and output `UploadCommand[]` / `DescriptorSlice[]` streams;
- systems use caller-owned spans and do not allocate;
- coalesced/compacted records remain ordinary Strict POD components;
- Vulkan runtimes consume the compacted ranges later but do not own ECS storage.

`UploadCoalesceSystem` only merges adjacent uploads with the same resource, upload kind, flags, and contiguous source/destination byte ranges. `DescriptorCompactionSystem` only merges adjacent slices with the same descriptor set id, generation, and contiguous descriptor table range.

## 19. Per-thread Vulkan command pools are runtime-only

Stage 10J adds `VulkanThreadCommandRuntime`.

Merge rule:

- ECS stores only `NativeCommandBufferRef`;
- per-thread command pool ownership stays in Render2D runtime slot metadata;
- do not add thread ids, `VkCommandPool`, `VkCommandBuffer`, or ThreadCenter types to ECS components;
- host scheduling may choose a thread index when allocating a command buffer, but that index is not part of the persistent component contract.

`VulkanCommandRuntime` remains the single-pool reference runtime. Use `VulkanThreadCommandRuntime` when command recording is distributed across worker threads.

## 20. Stage 10 is closed

Stage 10 is complete as of 2026-06-09.

Completed merge-relevant performance/runtime items:

- test/bench framework and Perf preset;
- fast_math-only render math;
- dirty transform/bounds updates;
- explicit draw sort and packed batch key;
- ThreadCenter runtime infrastructure and threaded sprite CPU pipeline facade;
- upload/descriptor stream compaction;
- per-thread Vulkan command pool runtime.

Future work belongs to a new stage, especially ThreadCenter-backed text work, parallel batch/sort tail stages, swapchain/window presentation, deferred destroy queues, and production font/atlas integration.

## 21. Component stream sizes are U32-bounded

Render2D component ranges, indices, and `SystemResult` counts use `U32`.

Merge rule:

- host ECS streams handed to Render2D systems must have at most `0xFFFFFFFF` elements;
- systems reject non-representable stream sizes with `SystemStatusCode::InvalidInput`;
- split larger host-engine datasets before calling Render2D systems;
- do not widen component indices without a deliberate component contract migration.

## 22. Threaded CPU pipeline should be threshold-gated

Stage 10H benchmark evidence shows ThreadCenter overhead is not free:

- 10k high-visibility sprite workloads were slower through the threaded runtime on the local Perf benchmark;
- 10k low-visibility was near parity and should be treated as noise-sensitive rather than a guaranteed win;
- 100k sprite workloads showed benefit, about 1.11x high visibility and 1.11x low visibility on the recorded run.

Merge rule:

- keep single-thread systems as the default small-workload path;
- enable `ThreadedCpuPipelineRuntime` only when host-side workload size justifies scheduling and merge overhead;
- tune `worker_count` and `min_items_per_task` per platform.

Status (Stage 21E): implemented. `ThreadedCpuPipelineConfig::parallel_threshold` (default `kDefaultParallelThreshold = 32768`) plus the shared `shouldParallelizeItemCount` gate in `include/Render2D/System/ParallelPolicy.hpp` route sub-threshold or single-worker workloads to the single-thread reference path automatically. The host should tune the threshold per platform; the threaded benchmark (`render2d_threaded_cpu_pipeline_bench`) forces the threaded path for measurement.

## 23. Deferred destroy is a runtime queue, not ECS ownership

Stage 11 adds `DeferredDestroyCommand` as an ECS-visible POD record and `NativeDeferredDestroyRuntime` as the runtime-owned pending queue.

Merge rule:

- host ECS may store `DeferredDestroyCommand[]`, but it does not own backend destruction;
- actual Vulkan release must still go through the native runtime that owns the resource slot;
- drain deferred commands only after the retire frame is reached or the relevant fence is known complete;
- keep IDs/generations in every deferred command so stale releases can be rejected by the target runtime.

## 24. Frame/present components are POD state records

Stage 11 adds `SwapchainImageRef`, `AcquiredImage`, and `PresentCommand`.

Merge rule:

- window and surface ownership remain in the host engine;
- component records carry swapchain/image/frame/sync IDs and generations only;
- Vulkan handle fields are non-owning snapshots for native resolution/debugging, not lifetime authority;
- acquire/present runtime integration must preserve these records without adding production ECS storage inside Render2D.

## 25. Swapchain boundary is host-surface-first

Stage 11 adds `VulkanSwapchainRuntime`.

Merge rule:

- host engine owns window creation and `VkSurfaceKHR`;
- Render2D may create a swapchain from that surface or adopt a host-created swapchain;
- image views created for swapchain images are runtime-owned;
- swapchain image memory is owned by Vulkan swapchain internals, not by Render2D GPU allocation;
- ECS-visible state remains `SwapchainState[]` and `SwapchainImageRef[]` with id + generation validation.

## 26. Acquire/present uses sync id + generation

Stage 11 adds `VulkanPresentRuntime` and extends `AcquiredImage` / `PresentCommand` with sync generations.

Merge rule:

- acquire uses `FrameSync.image_available` through `VulkanSyncRuntime`;
- present waits on `FrameSync.render_finished` through `VulkanSyncRuntime`;
- `PresentCommand` must carry `wait_sync_id + wait_sync_generation`;
- do not store raw semaphores or queues in ECS components;
- stale sync or stale swapchain records must be rejected before calling Vulkan.

## 27. Acquire-to-present is a component-stream transform

Stage 11E adds `PresentCommandBuildSystem`.

Merge rule:

- host ECS stores `AcquiredImage[]` and `PresentCommand[]`;
- Render2D only transforms spans and never owns production ECS storage;
- invalid acquired records with zero swapchain or sync generation must be rejected before presentation;
- present runtime must resolve the current `SwapchainState` and validate image index before returning/presenting;
- window-visible capture remains a host-engine integration proof because window/surface ownership is external.

## 28. Stage 11 native frame boundary is complete

Stage 11F closes the native frame-loop boundary.

Merge rule:

- integrate Render2D as a backend runtime behind your engine ECS;
- migrate the POD component streams directly into the host ECS;
- keep window/surface ownership in the host;
- use Render2D runtime APIs only to resolve ids/generations into Vulkan objects and perform backend operations;
- run host-side visible capture separately if you need swapchain/window proof.

## 29. Sprite GPU instance data is still ECS component data

Stage 12 adds `SpriteVertex`, `SpriteInstance`, `SpriteDrawPacket`, and `SpriteInstanceBuildSystem`.

Merge rule:

- host ECS may own `SpriteInstance[]` exactly like other component streams;
- `SpriteInstanceBuildSystem` writes by `DrawCommand::instance_first`, so sorted draw commands can preserve stable instance references;
- the 2D affine data is stored as six floats, not a runtime object and not a Vulkan handle;
- GPU upload must treat `SpriteInstance[]` as source component data and use MemoryCenter-backed buffers in the runtime path;
- pipeline/descriptors/shaders remain runtime concerns, not ECS ownership.

## 30. Sprite instance upload is typed ECS command plus runtime copy

Stage 12C adds `SpriteInstanceUploadCommand`, `SpriteInstanceUploadSystem`, and `VulkanSpriteInstanceUploadRuntime`.

Merge rule:

- host ECS owns `SpriteInstanceUploadCommand[]` and `UploadCommand[]`;
- each sprite upload command carries destination buffer id + generation, not a raw owning GPU allocation;
- `SpriteInstanceUploadSystem` only computes byte ranges and generic upload descriptors; it does not allocate or call Vulkan;
- `VulkanSpriteInstanceUploadRuntime` writes to the MemoryCenter-backed upload ring and records a copy into a `VulkanResourceRuntime` managed buffer;
- direct Vulkan memory allocation/mapping remains forbidden in the sprite path.

## 31. Sprite pipeline layout is runtime contract, not ECS storage

Stage 12D adds `VulkanSpritePipelineConfig` and `VulkanSpritePipelineRuntime`.

Merge rule:

- host ECS still owns `SpriteVertex[]`, `SpriteInstance[]`, draw/batch/upload streams, and resource ref components;
- sprite vertex input uses `SpriteVertex` as vertex-rate binding 0 and `SpriteInstance` as instance-rate binding 1;
- descriptor layout currently exposes sampled textures through combined image sampler descriptors;
- instance buffers are bound as vertex/instance buffers, not as descriptor-owned storage;
- `PipelineRef` and `DescriptorSlice` remain id + generation records resolved by native runtime.

## 32. Sprite render encoder is runtime-only

Stage 12E adds `VulkanSpriteRenderEncoder`.

Merge rule:

- host ECS still owns `SpriteVertex[]`, `SpriteInstance[]`, draw/batch streams, and POD refs;
- encoder receives resolved component refs and native runtimes, but does not own ECS storage;
- vertex data binds at slot 0 and instance data binds at slot 1, matching `VulkanSpritePipelineRuntime`;
- `PipelineRef`, `BufferRef`, `ImageRef`, `DescriptorSlice`, and `NativeCommandBufferRef` stay id + generation records;
- current smoke shader outputs instance color only; sampled texture/material descriptor policy remains a later integration step.

## 33. Stage 12 sprite GPU path is closed

Stage 12 is complete as of 2026-06-10.

Completed merge-relevant items:

- sprite GPU-facing POD records;
- `SpriteInstanceBuildSystem`;
- typed sprite instance upload command conversion;
- MemoryCenter-backed upload-ring copy into managed GPU buffers;
- sprite descriptor/pipeline layout;
- offscreen real sprite draw smoke through `VulkanSpriteRenderEncoder`.

Future work belongs to later stages: production texture atlas ownership, sampler/descriptor update policy, material selection, batching across multiple textures/materials, and Vulkan text draw integration.

## 34. Sampler refs are ECS components; VkSampler ownership is runtime-only

Stage 13 adds `SamplerRef`.

Merge rule:

- host ECS may store `SamplerRef[]` exactly like `ImageRef[]` or `BufferRef[]`;
- every sampler record uses `sampler_id + generation`;
- real `VkSampler` creation/destruction stays in `VulkanSamplerRuntime`;
- stale sampler refs must be rejected before descriptor updates;
- do not move `VkSampler` ownership, sampler caches, or runtime slot arrays into ECS components.

## 35. Textured sprite sampling is proven, but atlas/material policy is still host-facing work

Stage 13 proves a real sampled sprite path:

- upload buffer -> sampled image copy is recorded by `VulkanResourceRuntime`;
- descriptor update uses `DescriptorSlice` plus a resolved `SamplerRef`;
- `VulkanSpriteRenderEncoder` binds the descriptor slice and draws the sprite;
- the smoke verifies sampled green pixels by readback.

Merge rule:

- texture images and upload/readback buffers remain MemoryCenter-backed runtime resources;
- ECS owns only POD refs and component streams;
- production atlas packing, material selection, texture-array indexing, and multi-texture batch policy still need a dedicated host/runtime integration pass.

## 36. Texture/material generations must flow through sprite batches

Stage 14 adds generation fields to sprite resource identities.

Merge rule:

- host ECS should store `texture_id + texture_generation` and `material_id + material_generation` in sprite-facing records;
- `DrawCommand`, `BatchCommand`, `SpriteInstance`, and `SpriteDrawPacket` must preserve those generations;
- `DrawCommand.sort_key` remains only a fast grouping hint;
- batch merge must still compare the full id + generation identity to avoid merging stale/reused resource ids.

## 37. SpriteDrawPacket is the ECS-visible draw binding record

Stage 14 adds `SpriteMaterialBinding`, `SpriteTextureBinding`, `SpriteDrawPacketBuildSystem`, and multi-packet sprite encoding.

Merge rule:

- host ECS owns `SpriteMaterialBinding[]`, `SpriteTextureBinding[]`, and `SpriteDrawPacket[]`;
- material bindings map material id + generation to pipeline id + generation;
- texture bindings map texture id + generation to descriptor id + generation plus descriptor range;
- `VulkanSpriteRenderEncoder::recordPackets` only resolves packet refs and records Vulkan commands;
- current packet builder requires contiguous instance ranges inside a batch before emitting one instanced draw packet.

Production atlas packing, material graph policy, descriptor indexing, and text rendering remain separate future integration work.

## 38. Texture atlas regions are ECS components; atlas images remain runtime/host resources

Stage 15 adds `TextureAtlasItem`, `TextureAtlasRegion`, `TextureAtlasBuildConfig`, `TextureAtlasBuildSystem`, and `Sprite.texture_region_id + texture_region_generation`.

Merge rule:

- host ECS owns atlas item/region streams and sprite region references;
- `TextureAtlasBuildSystem` is a deterministic no-allocation shelf packer over caller-owned spans;
- `TextureAtlasRegion` stores atlas id/generation, texture id/generation, pixel rect, and normalized UVs;
- `SpriteInstanceBuildSystem::runWithTextureRegions` writes region UVs into `SpriteInstance[]` and rejects stale or texture-mismatched regions;
- actual atlas images, uploads, raster data, descriptor updates, and advanced packing policy remain outside ECS components and should be owned by host/runtime integration.

## 39. Atlas region UVs are GPU-proven, but production atlas ownership is still external

Stage 16 adds a Vulkan smoke test that renders two sprites from one 2x1 atlas texture through one descriptor and one packet.

Merge rule:

- host/runtime still owns atlas image creation, upload scheduling, descriptor updates, and retirement;
- host ECS still owns `TextureAtlasRegion[]`, `Sprite[]`, `DrawCommand[]`, and `SpriteInstance[]`;
- Render2D systems can now be trusted to propagate region UVs into the sampled sprite path;
- this proof does not replace a production atlas runtime, image packing policy, font raster ingestion, material graph, or bindless/descriptor-indexing design.
