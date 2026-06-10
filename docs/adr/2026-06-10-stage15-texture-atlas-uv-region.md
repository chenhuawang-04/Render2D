# ADR: Stage 15 Texture Atlas UV Region Foundation

Date: 2026-06-10

## Status

Accepted

## Context

Stage 14 can render multiple sprite texture packets in one dynamic-rendering pass, but `SpriteInstance` UVs still default to the full texture. The next required boundary is an ECS-visible way to describe atlas regions and propagate those regions into GPU-facing sprite instances without making Render2D own the production ECS or atlas image storage.

## Decision

- Add `texture_region_id + texture_region_generation` to `Sprite`.
- Add Strict POD atlas contracts:
  - `TextureAtlasBuildConfig`
  - `TextureAtlasItem<Provider, Dim>`
  - `TextureAtlasRegion<Provider, Dim>`
- Add `TextureAtlasBuildSystem`, a deterministic shelf packer that:
  - consumes `TextureAtlasItem[]`;
  - writes caller-owned `TextureAtlasRegion[]`;
  - performs no allocation;
  - does not call Vulkan;
  - stores both pixel rects and normalized UVs.
- Add `SpriteInstanceBuildSystem::runWithTextureRegions`.
  - `texture_region_id == 0 && texture_region_generation == 0` keeps full-texture UVs.
  - Nonzero region refs are resolved by `region_id + generation`.
  - Region texture id/generation must match the `DrawCommand` texture identity.

## Consequences

- Atlas items and regions are ECS-owned component streams, not Render2D runtime storage.
- The host engine can later replace test storage and feed the same spans from its ECS.
- Atlas images, texture uploads, raster data, descriptor updates, advanced bin packing, and material graph policy remain future runtime/host integration work.
- The current shelf packer is intentionally simple and deterministic; it is suitable for tests and basic region layout, not final production packing policy.
