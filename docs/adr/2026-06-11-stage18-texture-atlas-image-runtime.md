# ADR: Stage 18 Texture Atlas Image Runtime

Date: 2026-06-11

## Status

Accepted

## Context

Stage 15/16 proved the ECS-side texture atlas path: `TextureAtlasBuildSystem` packs `TextureAtlasItem[]` into `TextureAtlasRegion[]`, `SpriteInstanceBuildSystem::runWithTextureRegions` propagates region UVs into `SpriteInstance[]`, and the Stage 16 smoke samples those UVs through the real sprite shader. But the atlas *image* in that proof was created and uploaded with ad-hoc `VulkanResourceRuntime` calls (a single whole-image copy). There was no runtime that owns an atlas image's lifetime or composites distinct sub-images into sub-rectangles. Stage 18 adds that image-side runtime without disturbing the proven ECS contracts.

## Decision

- Add `VulkanTextureAtlasRuntime<Provider, Dim>` (`include/Render2D/Native/VulkanTextureAtlasRuntime.hpp`). It owns atlas image slots behind a `VulkanAtlasImageRef` `id + generation`, validates generations, and rejects stale references after slot reuse — the same slot-table pattern as the other native runtimes.
- The backing sampled image's allocation/lifetime is **delegated to `VulkanResourceRuntime`** (MemoryCenter-backed). The atlas runtime stores the returned `ImageRef` and binds a non-owning `VulkanResourceRuntime*` at `initialize`; `createAtlasImage` forces `SAMPLED | TRANSFER_DST | TRANSFER_SRC` usage, and `releaseAtlasImage`/`shutdown` release the image back through the resource runtime. The resource runtime must outlive the atlas runtime.
- `VulkanAtlasImageRef`, `VulkanAtlasImageConfig`, and `VulkanTextureAtlasRuntimeConfig` are runtime POD types, **not** ECS components. ECS-facing atlas identity stays in the existing `TextureAtlasItem` / `TextureAtlasRegion` records, so no component contract changes.
- Add `VulkanResourceRuntime::recordCopyBufferToImageRegion` — a sub-rectangle buffer→image copy (`imageOffset`/`imageExtent`) with region-bounds, format, and source-range validation, plus a `makeRegionByteCount` helper. Sub-image copy logic stays centralized in the resource runtime (the image owner); the atlas runtime's `recordUploadRegion` only resolves the atlas → backing `ImageRef` and forwards.
- Prove the path end to end (18E): a sibling smoke in `tests/vulkan_textured_sprite_render_test.cpp` creates the atlas image via `VulkanTextureAtlasRuntime`, composites two 1x1 sub-images into a 2x1 atlas with `recordUploadRegion`, then samples them through region UVs and verifies left-red / right-green readback.
- Improve packing (18C): add `TextureAtlasBuildSystem::runHeightSorted`, a First-Fit Decreasing Height shelf packer over a caller-owned scratch order span. It packs varied-height items far tighter than the input-order `run()` shelf while staying deterministic and allocation-free; regions are looked up by id, so placement order stays unobservable to consumers.
- Support reuse (18D): add `VulkanTextureAtlasRuntime::retireAtlasImage`, which frees the atlas handle immediately (slot/id reusable at once) but hands the backing image to `NativeDeferredDestroyRuntime` so it survives in-flight frames; the caller drains the queue after the retire frame and releases each backing image through the resource runtime.

## Consequences

- An atlas image now has an owning runtime with `id + generation` validation, and distinct sub-images can be composited into one atlas via sub-rectangle uploads.
- All GPU allocation still flows through `VulkanResourceRuntime` + `VulkanMemoryCenterAllocator`; the atlas runtime adds no direct Vulkan memory calls and no production ECS storage.
- The atlas image handle is intentionally a runtime POD for now. If a later stage needs the atlas image as an ECS-visible component, that is a deliberate component-contract change with its own ADR.
- Stage 18 is closed. The atlas runtime owns image lifetime (create, sub-region composite, resolve, immediate release, and frame-safe deferred retirement) behind id + generation, and ships an FFDH packer alongside the original shelf packer. Production packing beyond FFDH (skyline/MaxRects), font raster ingestion, descriptor-update policy, and bindless/descriptor-indexing remain later host/runtime work.
