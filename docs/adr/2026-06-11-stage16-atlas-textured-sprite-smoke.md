# ADR: Stage 16 Atlas Textured Sprite Smoke

Date: 2026-06-11

## Status

Accepted

## Context

Stage 15 introduced texture atlas region components and CPU-side UV propagation, but the proof stopped before the sampled Vulkan shader. We need to verify that region UVs generated from ECS-owned atlas data survive through `SpriteInstance[]`, the instance vertex buffer, descriptor binding, and the textured sprite shader.

## Decision

Add a Vulkan smoke path to `tests/vulkan_textured_sprite_render_test.cpp` that:

- creates one 2x1 sampled atlas image with red and green texels;
- builds two `TextureAtlasRegion` records via `TextureAtlasBuildSystem`;
- creates two sprites referencing those regions by `region_id + generation`;
- builds `SpriteInstance[]` through `SpriteInstanceBuildSystem::runWithTextureRegions`;
- renders both instances in one `SpriteDrawPacket` using one descriptor;
- verifies readback: left half red, right half green.

## Consequences

- The atlas-region UV path is now proven through the real sampled sprite shader.
- A single atlas texture can feed multiple sprite regions without rebinding descriptors.
- This is still a smoke proof, not a production atlas runtime. Atlas image ownership, asset packing, font raster ingestion, descriptor update policy, material graph selection, and bindless/descriptor-indexing policy remain later host/runtime work.
