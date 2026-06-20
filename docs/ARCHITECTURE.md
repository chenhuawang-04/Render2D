# Render2D Architecture

Render2D is a C++23, component-first, Vulkan-native rendering module. The current implementation keeps ECS-visible data as Strict POD streams while native runtimes own Vulkan object lifetimes behind `id + generation` references.

## Scope and non-goals

**Render2D is a 2D *rendering module*, not a game engine.** It is deliberately scoped to turn render data (transforms, sprites, text, cameras) into batched, submitted GPU work, and nothing else. It is designed to be merged into a host engine, which owns everything around the renderer.

The following subsystems are **intentionally NOT part of Render2D** and will never be added here — they are the **host engine's responsibility** and are filled in at merge time (see `docs/ProjectMergeTODO.md` for the 39 numbered constraints and `docs/MERGE_GUIDE.md` for the ordered integration walkthrough):

- **Application & windowing** — the OS window, surface creation, and the main/event loop. Render2D renders into a surface/target the host provides; it does not own a window or run a loop.
- **Input** — keyboard, mouse, gamepad, and touch.
- **Audio** — playback, mixing, spatialization.
- **Production ECS / scene graph / entity management** — the ECS under `tests/support/` is *test-only scaffolding*. The host engine's ECS replaces it; Render2D systems only consume non-owning `std::span` streams.
- **Asset pipeline** — loading textures/fonts/scenes from disk, import/cooking, hot-reload, and serialization/save. Render2D consumes already-resident bytes and `id + generation` handles.
- **Animation** — sprite-frame, skeletal, and tweening systems.
- **Physics & collision.**
- **Gameplay & scripting** — game logic, scripting VMs, state machines.
- **High-level 2D content** — particles, tilemaps, and a UI/widget framework.
- **Editor & tooling** — level/scene editors and authoring tools.
- **Networking.**

What Render2D *does* own is the rendering layer below all of the above: the Strict POD render components, the deterministic CPU systems that transform/cull/batch/sort them, and the Vulkan runtimes that own backend object lifetimes. When evaluating completeness, measure Render2D against "a complete 2D *renderer*", not "a complete 2D *engine*" — the breadth above is delegated by design, not missing.

## Core principles

1. **ECS owns components.** All render data records are ECS components, including `VisibleItem`, `DrawCommand`, `BatchCommand`, `UploadCommand`, and `NativeSubmitCommand`.
2. **Components are Strict POD.** Components are trivial, standard-layout, trivially copyable, and aggregate. They do not own resources and do not contain `std::string`, `std::vector`, `std::span`, RAII wrappers, constructors, destructors, or virtual functions.
3. **Systems do not own ECS storage.** Production systems consume and write component streams through non-owning boundaries such as `std::span`.
4. **Provider/Dim are compile-time tags.** The current valid domain is `VulkanNativeProvider + Dim2`.
5. **Memory is centralized.** Render2D-owned dynamic CPU arrays use `Render2D::McVector` backed by MemoryCenter/Vector_New; Vulkan buffer/image backing memory is allocated and synchronized through `VulkanMemoryCenterAllocator`.
6. **Math is centralized through fast_math.** Render2D math aliases (`Vec2`, `Mat3`, `Aabb2`) are `MMath` Strict POD types, and render math systems call fast_math free functions.
7. **Native runtime owns backend lifetimes.** ECS stores POD handles/IDs. Runtime tables own and validate backend slots behind those refs.
8. **Stream sizes are U32-bounded.** Component indices, ranges, and `SystemResult` counts are `U32`; systems reject component streams whose sizes cannot be represented by `U32`.

## Current data pipeline

```text
Sprite path:
Transform[] / Sprite[] / Camera[]
    -> TransformSystem
    -> BoundsSystem
    -> CullingSystem
    -> CommandBuildSystem
    -> DrawCommand[]

Optional atlas path:
TextureAtlasItem[]
    -> TextureAtlasBuildSystem
    -> TextureAtlasRegion[]

Text path:
Text[] + TextState[] + FontAtlasRef[]
    -> TextDirtySystem
    -> TextDirtyRange[]
    -> GlyphRunBuildSystem
    -> GlyphInstanceBuildSystem
    -> GlyphBatchSystem
    -> DrawCommand[]

DrawCommand[]
    -> DrawSortSystem (optional)
    -> DrawCommand[]
    -> SpriteInstanceBuildSystem / runWithTextureRegions
    -> SpriteInstance[]
    -> BatchSystem
    -> BatchCommand[]

Native-bound side streams:
SpriteInstanceUploadCommand[] + SpriteInstance[]
    -> SpriteInstanceUploadSystem
    -> UploadCommand[]
UploadCommand[]
    -> UploadCoalesceSystem
    -> UploadCommand[]
DescriptorSlice[]
    -> DescriptorCompactionSystem
    -> DescriptorSlice[]

BatchCommand[] + UploadCommand[] + DescriptorSlice[]
    -> CommandBufferBuildSystem
    -> CommandBuffer[]
    -> EncodeSystem
    -> NativeCommandBufferRef[]
    -> SubmitSystem
    -> NativeSubmitCommand[]
```

The CPU component pipeline remains ECS-driven. Vulkan command, sync, submit, resource, descriptor, pipeline, upload-ring, sampler, and encoder runtimes sit behind POD references. The current offscreen smoke paths validate both generic dynamic rendering and sprite textured sampling through readback.

## Component layer

The component layer defines Strict POD ECS records:

- Scene/input components: `Transform`, `Sprite`, `Text`, `Utf8Slice`, `Camera`, `LocalBounds`, `VisibilityMask`, `RenderLayer`, `MaterialRef`, `TextureRef`, `FontRef`, `FontAtlasRef`, `TextureAtlasItem`.
- Derived components: `WorldTransform`, `WorldBounds`, `VisibleItem`, `SortedItem`, `SpriteVertex`, `SpriteInstance`, `SpriteDrawPacket`, `SpriteMaterialBinding`, `SpriteTextureBinding`, `TextureAtlasRegion`, `TextState`, `TextDirtyRange`, `GlyphRun`, `GlyphInstance`.
- Command components: `DrawCommand`, `BatchCommand`, `SpriteInstanceUploadCommand`, `UploadCommand`, `NativeSubmitCommand`, `CommandBuffer`.
- Frame/native state components: `FrameIndex`, `FrameArenaState`, `DescriptorSlice`, `UploadRingSlice`, `FenceState`.
- Native resource references: `DeviceHandle`, `QueueHandle`, `SwapchainState`, `FrameSync`, `NativeCommandBufferRef`, `PipelineRef`, `ImageRef`, `SamplerRef`, `BufferRef`, `UploadSlice`.

Every supported component is registered through `ComponentTraits` and checked by `SupportedRenderComponent`. `WorldTransform` stores `Render2D::Mat3` (`MMath::Mat3`), while `LocalBounds`, `WorldBounds`, and `GlyphInstance::atlas_rect` store `Render2D::Aabb2` (`MMath::Aabb2`). Use `makeAabb2` / `aabb2Min` / `aabb2Max` instead of touching AABB internals.

## System layer

Systems are stateless template types. They:

- take input component streams as `std::span<const T>`;
- take output component streams as `std::span<T>`;
- return `SystemResult`;
- do not allocate in the hot path;
- do not depend on ECS storage implementation;
- do not call Vulkan.

This keeps systems reusable when the temporary test ECS is replaced by the host engine ECS.

Stage 9 text systems are dependency-free and deterministic. `TextDirtySystem` emits dirty glyph ranges, `GlyphRunBuildSystem` and `GlyphInstanceBuildSystem` can update only those ranges, and `GlyphBatchSystem` emits regular `DrawCommand[]` entries over `GlyphInstance[]`. Real UTF-8 decoding, shaping, rasterization, atlas packing, and FreeType linkage are deferred to a dedicated font runtime stage.

Stage 10C migrated transform, bounds, culling, and atlas-rect math to fast_math. `BoundsSystem` now uses the fast center/extents transform formula over `MMath::Mat3` / `MMath::Aabb2` instead of Render2D-owned math structs.

Stage 10F adds packed draw sort keys and an optional stable radix `DrawSortSystem`. Sorting reduces batch counts when command streams are not already resource-grouped, but it is explicit (`--enable-sort` in benchmarks) because CPU-only sort cost is measurable.

Stage 10G integrates ThreadCenter as runtime/system infrastructure only. The repository now embeds `Center.Thread.Headers` and exposes it through an internal `render2d_thread_runtime_support` target used by a smoke test. ECS components, system signatures, and the public `Render2D::Render2D` interface remain ThreadCenter-free at this stage.

Stage 10H adds `ThreadedCpuPipelineRuntime`, a ThreadCenter-backed runtime facade for the sprite CPU path. It parallelizes Transform, Bounds, Culling, and CommandBuild over fixed chunks, writes per-chunk culling scratch with `McVector`, merges visible items in chunk order, then runs BatchSystem through the existing single-thread reference path. It does not add ECS components and is intentionally not included by the umbrella header because it requires the internal ThreadCenter support target.

Stage 10I adds allocation-free stream compaction for native-bound data. `UploadCoalesceSystem` merges adjacent contiguous `UploadCommand[]` records with the same resource/kind/flags, and `DescriptorCompactionSystem` merges adjacent contiguous `DescriptorSlice[]` records with the same descriptor set/generation. These outputs remain ECS component streams and are not native storage.

Stage 10J adds `VulkanThreadCommandRuntime`, a Vulkan-native runtime that owns one command pool per runtime thread slot. It allocates command buffers from the requested thread pool and returns ordinary `NativeCommandBufferRef` id/generation records; thread ownership stays in runtime metadata and does not enter ECS components.

Stage 12A/12B starts the production sprite GPU path. `SpriteVertex`, `SpriteInstance`, and `SpriteDrawPacket` are Strict POD component records. `SpriteInstanceBuildSystem` converts `DrawCommand[]`, `WorldTransform[]`, and `Sprite[]` into `SpriteInstance[]`, writing each record at `DrawCommand::instance_first` so sorted draw commands can keep stable instance references. It does not allocate and does not call Vulkan.

Stage 12C adds the sprite instance upload path. `SpriteInstanceUploadCommand` is an ECS-owned POD command carrying an instance range plus destination buffer id/generation, destination offset, and frame index. `SpriteInstanceUploadSystem` converts those typed commands into generic `UploadCommand[]` for coalescing/range description. `VulkanSpriteInstanceUploadRuntime` then allocates and writes a MemoryCenter-backed upload-ring slice and records a copy from the upload ring into the managed GPU buffer through `VulkanResourceRuntime`.

Stage 12D adds the sprite pipeline and descriptor layout contract. `VulkanGraphicsPipelineConfig` now accepts optional vertex binding/attribute descriptions. `VulkanSpritePipelineRuntime` is a stateless helper that fixes the sprite vertex input layout: `SpriteVertex` is vertex-rate binding 0 and `SpriteInstance` is instance-rate binding 1. The descriptor layout currently uses a combined image sampler array; instance data remains a vertex/instance buffer, not a descriptor-owned ECS storage path.

Stage 12E adds `VulkanSpriteRenderEncoder`, a runtime-only dynamic rendering recorder for the sprite vertex/instance path. It resolves POD refs by id + generation, binds vertex buffer slot 0 and instance buffer slot 1, optionally binds descriptor slices, records `vkCmdDraw`, and leaves ECS ownership of `SpriteVertex[]`, `SpriteInstance[]`, refs, and command streams outside Render2D runtime storage.

Stage 13 adds the first real sampled sprite path. `SamplerRef` is a Strict POD ECS-visible resource reference, while `VulkanSamplerRuntime` owns `VkSampler` lifetimes. `VulkanResourceRuntime::recordCopyBufferToImage` records tightly packed upload-buffer copies into managed sampled images. The textured sprite smoke updates a combined image sampler descriptor, draws through the sprite encoder, and verifies a green 4x4 offscreen target by readback.

Stage 14 turns the single-texture proof into a multi-packet sprite binding path. Texture/material identities now carry generation fields through `Sprite`, `DrawCommand`, `BatchCommand`, `SpriteInstance`, and `SpriteDrawPacket`. `SpriteMaterialBinding[]` and `SpriteTextureBinding[]` are ECS-owned POD streams that resolve batch identities to pipeline and descriptor references. `SpriteDrawPacketBuildSystem` emits packet records, and `VulkanSpriteRenderEncoder::recordPackets` draws multiple packets in one render pass while rebinding pipeline/descriptor only when the packet changes them.

Stage 15 adds the texture-atlas UV region foundation. `TextureAtlasItem[]` and `TextureAtlasRegion[]` are ECS-owned Strict POD streams, and `TextureAtlasBuildSystem` performs deterministic no-allocation shelf packing into caller-owned output spans. `Sprite` now carries `texture_region_id + texture_region_generation`; `SpriteInstanceBuildSystem::runWithTextureRegions` resolves that pair, validates the region texture id/generation against the `DrawCommand`, and writes region UVs into `SpriteInstance`. Atlas image creation/upload and advanced packing remain future runtime/host work.

Stage 16 proves the atlas UV path through the real Vulkan sampled sprite shader. The smoke test uploads a 2x1 atlas image, builds two atlas regions, generates `SpriteInstance[]` through `runWithTextureRegions`, and renders two instances in one packet through one descriptor. Readback verifies that the left and right screen halves sample different atlas regions from the same texture.

## Temporary test ECS

The repository includes test-only storage under `tests/support/`. This storage exists only to validate components and systems. It is not production architecture and must be replaced by the host engine ECS during integration. Its backing arrays use `Render2D::McVector`, but the storage itself remains test-only.

Test-only scaffolding extends this with a lightweight, usable face: `tests/support/MiniEcs.hpp` is a small generational-handle ECS (create/destroy entities, per-type sparse-set components, and `gatherRenderInputs()` packing row-aligned columns for the span-only systems), and `tests/support/AssetRegistry.hpp` + `tests/support/ImageFile.{hpp,cpp}` are a test-only asset framework that loads real image files (the vendored stb headers, compiled behind the `render2d_test_image_io` target — never in the core or umbrella). They exist only to prove the renderer is usable end-to-end from an author-it-yourself scene; like the storage above, they are merge-time removal targets, not production architecture, and an ECS / asset pipeline stay out of the core's scope. See `docs/adr/2026-06-19-test-ecs-asset-framework.md`.

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
SamplerRef::sampler_id + SamplerRef::generation
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
- `NativeDeferredDestroyRuntime` - runtime-owned queue for frame-safe deferred resource retirement.

Implemented Vulkan-backed runtimes:

- `VulkanCommandRuntime` - Vulkan command pool and command buffer lifecycle owner behind `NativeCommandBufferRef`.
- `VulkanThreadCommandRuntime` - per-thread Vulkan command pools and command buffer lifecycle ownership behind `NativeCommandBufferRef`.
- `VulkanSyncRuntime` - real semaphore/fence lifecycle behind `FrameSync`.
- `VulkanSubmitRuntime` - real `vkQueueSubmit` using resolved command buffers and frame sync.
- `VulkanSwapchainRuntime` - swapchain/image-view runtime for host-provided surfaces or adopted swapchains.
- `VulkanResourceRuntime` - real buffer/image/image-view lifecycle, MemoryCenter-backed GPU allocation, upload/readback, buffer/image copies, and image layout tracking.
- `VulkanSamplerRuntime` - real `VkSampler` lifecycle behind `SamplerRef`.
- `VulkanDescriptorRuntime` - descriptor pool, descriptor set layout, set allocation, and descriptor array updates.
- `VulkanPipelineRuntime` - shader module creation, pipeline cache, dynamic-rendering pipeline layout/pipeline creation.
- `VulkanPresentRuntime` - swapchain image acquire and queue present using resolved `SwapchainState`, `PresentCommand`, and `FrameSync`.
- `VulkanUploadRingRuntime` - MemoryCenter-backed persistent mapped, frame-segmented upload ring; slices are not reusable until the frame is completed.
- `VulkanSpriteInstanceUploadRuntime` - stateless bridge that uploads `SpriteInstance[]` through the upload ring into a managed GPU buffer.
- `VulkanSpritePipelineRuntime` - stateless sprite descriptor/pipeline layout helper over `VulkanDescriptorRuntime` and `VulkanPipelineRuntime`.
- `VulkanSpriteRenderEncoder` - runtime-only sprite dynamic rendering encoder that binds vertex/instance buffers and records direct sprite draws.
- `VulkanDynamicRenderEncoder` - records dynamic rendering, pipeline bind, direct draw, and indirect draw.

These runtime classes are not ECS storage. They own backend slot lifecycle metadata, validate generations, reject stale references, reuse slots, and keep Vulkan handles out of ECS components.

## Benchmarking

The Null CPU benchmark validates the current sprite/text/mixed component pipelines without Vulkan:

```powershell
cmake --preset clang-ninja-debug -DRENDER2D_BUILD_BENCHMARKS=ON
cmake --build build
.\build\bench\render2d_null_cpu_bench.exe --scenario mixed --sprites 10000 --texts 2048 --frames 8 --warmup 2
```

It reports active counts and average pass timings for sprite systems, text dirty/glyph systems, batching, and command buffer descriptor build. The Stage 10B standard suite is run with `scripts/run_null_cpu_benchmarks.ps1` and documented in `docs/architecture/BENCHMARK_BASELINE.md`. Stage 10C records the fast_math migration delta there: 10k sprite bounds dropped from about 3.64 ms to about 0.55 ms on the local Debug benchmark. Stage 10D adds a RelWithDebInfo Perf preset, release-like test assertion handling, dirty-transform scenarios, and large/huge local benchmark suites so later single-thread and ThreadCenter work has stable evidence. Stage 10E adds dirty-index Transform/Bounds updates and a zero-rotation transform fast path. Stage 10H adds `scripts/run_threaded_cpu_benchmarks.ps1` and `render2d_threaded_cpu_pipeline_bench`, which show ThreadCenter overhead on small high-visibility runs and benefit on 100k sprite runs.

Stage 10I also adds `render2d_upload_descriptor_compaction_bench`, a Perf benchmark for synthetic upload and descriptor stream compaction. The local 65,536-item run compacts each stream to 16,384 records and records average upload/descriptor compaction times in `BENCHMARK_BASELINE.md`.

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
- Stage 9A Text/Glyph Strict POD components: `Utf8Slice`, `GlyphRun`, `GlyphInstance`, and `FontAtlasRef`
- Stage 9B deterministic test glyph systems through `GlyphRunBuildSystem`, `GlyphInstanceBuildSystem`, and `GlyphBuildConfig`
- Stage 19 font/text runtime: FreeType, HarfBuzz, and SheenBidi as git submodules under `third_party/`, built behind `RENDER2D_BUILD_FONT_RUNTIME` and isolated in the internal `render2d_font_runtime_support` target (see `docs/architecture/STAGE19_TEXT_FONT_DESIGN.md`)
- Stage 10C fast_math migration: `Render2D::Vec2`, `Render2D::Mat3`, and `Render2D::Aabb2` alias `MMath` POD types; custom Render2D `Aabb2` / `Affine2X3` structs are removed
- Stage 10D benchmark/profile harness: `clang-ninja-perf` preset, dirty transform benchmark input mutation, extended runner suites, and Stage 10 TODO tracking
- Stage 10E single-thread spatial hot path: `TransformDirtyItem`, `TransformSystem::runDirty`, `BoundsSystem::runDirty`, and zero-rotation transform fast path
- Stage 10F sort/batch foundation: packed draw sort keys, optional `DrawSortSystem`, and collision-safe packed-key-first `BatchSystem` comparison
- Stage 10G ThreadCenter integration: header-only runtime/system dependency embedded in CMake through `render2d_thread_runtime_support`, with smoke coverage and no ECS/public-interface contamination
- Stage 10H ThreadCenter-backed CPU pipeline runtime: deterministic chunked sprite path with single-thread equivalence coverage
- Stage 10I stream compaction: `UploadCoalesceSystem`, `DescriptorCompactionSystem`, tests, and benchmark coverage
- Stage 10J per-thread Vulkan command runtime: one command pool per runtime thread slot with runtime-only ownership metadata
- Stage 10K Stage 10 closeout: final checklist, ADR, project index, and verification documentation
- Stage 11A native frame/present POD contracts: `SwapchainImageRef`, `AcquiredImage`, `PresentCommand`, and `DeferredDestroyCommand`
- Stage 11B Vulkan swapchain runtime: host-provided surface or adopted swapchain, image query, image-view ownership, and POD swapchain/image refs
- Stage 11C Vulkan acquire/present runtime: `vkAcquireNextImageKHR`, `AcquiredImage`, `PresentCommand`, and `vkQueuePresentKHR`
- Stage 11D deferred destroy foundation: `NativeDeferredDestroyRuntime` queues retire commands and drains only frame-safe records
- Stage 11E acquire/present state coverage: `PresentCommandBuildSystem`, acquire/present result mapping tests, and present-side swapchain image-index validation
- Stage 11F native frame-loop closeout: final documentation, merge guidance, and Debug/Perf verification
- Stage 12A/12B sprite GPU instance contracts: `SpriteVertex`, `SpriteInstance`, `SpriteDrawPacket`, and `SpriteInstanceBuildSystem`
- Stage 12C sprite instance GPU upload path: `SpriteInstanceUploadCommand`, `SpriteInstanceUploadSystem`, and `VulkanSpriteInstanceUploadRuntime`
- Stage 12D sprite descriptor/pipeline layout: optional vertex input in `VulkanGraphicsPipelineConfig`, `VulkanSpritePipelineConfig`, and `VulkanSpritePipelineRuntime`
- Stage 12E offscreen real sprite draw: `VulkanSpriteRenderEncoder`, embedded sprite smoke shaders, and readback verification over `SpriteVertex` + `SpriteInstance` buffers
- Stage 12F sprite GPU path closeout: documentation, ADR, project index, Debug/Perf verification, clang-tidy, and source constraint scans
- Stage 13A sampler POD/runtime path: `SamplerRef` and `VulkanSamplerRuntime`
- Stage 13B texture upload recording: `VulkanResourceRuntime::recordCopyBufferToImage`
- Stage 13C textured sprite smoke: combined image sampler descriptor update, sampled sprite shader, and green readback verification
- Stage 13D textured sprite sampling closeout: documentation, ADR, project index, Debug/Perf verification, clang-tidy, and source constraint scans
- Stage 14A resource-generation sprite contracts: texture/material generation fields across sprite, draw, batch, instance, and packet records
- Stage 14B packet build system: `SpriteMaterialBinding`, `SpriteTextureBinding`, and `SpriteDrawPacketBuildSystem`
- Stage 14C multi-packet encoder: `VulkanSpriteRenderEncoder::recordPackets`
- Stage 14D multi-texture smoke: red/green descriptor batches in one command buffer with readback verification
- Stage 15A texture atlas POD contracts: `TextureAtlasItem`, `TextureAtlasRegion`, `TextureAtlasBuildConfig`, and `Sprite` region id/generation
- Stage 15B deterministic shelf atlas packing through `TextureAtlasBuildSystem`
- Stage 15C sprite instance UV region propagation through `SpriteInstanceBuildSystem::runWithTextureRegions`
- Stage 16A atlas-region textured sprite smoke: one atlas image, one descriptor, one packet, two region-backed instances, and red/green readback verification

Stages 17–25 (reinforcement roadmap — see `ReinforcementPlan.md` and `docs/PROJECT_INDEX.md` for the file-level inventory):

- Stage 17 build portability + CI: three-tier engine-dependency resolution (`cmake/Render2DDependencies.cmake`), CMake presets, and the `.github/workflows/ci.yml` portable/full tiers
- Stage 18 atlas image runtime: `VulkanTextureAtlasRuntime` and the atlas image/region contracts
- Stage 19 font/text runtime: real UTF-8 decoding, SheenBidi itemization, HarfBuzz-over-FreeType shaping + glyph rasterization (`FontShapeRuntime`), glyph atlas residency/packing (`GlyphAtlasRuntime`), and an end-to-end Vulkan glyph-coverage text draw (`tests/vulkan_glyph_text_render_test.cpp`)
- Stage 20 bindless: descriptor-indexing mechanism for the sprite/material path (the production indexing *policy* stays the host's)
- Stage 21 parallel tail: ThreadCenter-backed `ThreadedTextCpuPipelineRuntime` (glyph-instance build), `ThreadedDrawSortRuntime` (radix sort), and `ThreadedBatchRuntime` — each byte-identical to its single-thread reference
- Stage 22 present-host: optional, isolated SDL3 window + `VkSurfaceKHR` provider (`RENDER2D_BUILD_PRESENT_HOST`), on-screen present loop, visible-capture == offscreen baseline, and RenderDoc frame capture
- Stage 23 host-merge readiness: the `tests/support/HostLikeEcs.hpp` byte-identical adapter, the end-to-end `host_present_frame` capstone, and `docs/MERGE_GUIDE.md`
- Stage 24 CPU refinement: the fused `SpatialCullSystem` front-end and the AVX2/FMA build option (`RENDER2D_ENABLE_AVX2`)
- Stage 25 consumability/packaging: `find_package(Render2D)` install/export and the downstream-consumer smokes
- Stage 26 reproducibility/release closeout: engine deps made public + pinned to exact commits, the `full-build` CI auto-gate, and the tagged `v0.1.0` release (see `CHANGELOG.md`)

Not implemented yet (host-engine or production-policy responsibilities — deliberately out of scope, see *Scope and non-goals*):

- host-engine window-visible capture automation (the in-repo, optional Stage 22 SDL3 present-host demonstrates it — 22C present-loop, 22D visible-capture == offscreen baseline; the host still owns window/surface and its own automation at merge, `RENDER2D_BUILD_PRESENT_HOST=OFF`; the present tests + a `MiniEcs`/`AssetRegistry` scene→window demo share the test-only `tests/support/WindowTestHarness.hpp`)
- production atlas runtime ownership, raster-data ingestion, advanced bin packing, complex material graph, and the production bindless/descriptor-indexing policy (Stage 20 provides the mechanism; the host owns the policy)
- production GPU text draw integration into the host's render graph (the in-repo Stage 19F text draw proves the path; the host owns its own pass wiring)
- RenderDoc / frame-capture automation in the host's pipeline (Stage 22E adds programmatic `.rdc` capture of a real present frame via the optional present-host's `RenderDocCapture`; a host supplies its own automation at merge)
