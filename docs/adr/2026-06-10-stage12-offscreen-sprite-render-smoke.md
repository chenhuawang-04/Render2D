# ADR: Stage 12 Offscreen Sprite Render Smoke

Status: Accepted

Date: 2026-06-10

## Context

Stage 12A-12D established sprite GPU-facing POD records, instance building, instance upload, and a sprite vertex-input pipeline. The remaining proof is a real Vulkan draw using `SpriteVertex` and `SpriteInstance` buffers, without moving ECS component ownership into Render2D runtime storage.

## Decision

Add:

```text
include/Render2D/Native/VulkanSpriteRenderEncoder.hpp
tests/vulkan_sprite_render_encoder_test.cpp
tests/support/SpriteShaders.hpp
```

`VulkanSpriteRenderEncoder` is a runtime-only command recorder. It resolves `NativeCommandBufferRef`, `ImageRef`, `PipelineRef`, `BufferRef`, and optional `DescriptorSlice` records by id + generation through existing runtimes. It transitions the color target to color attachment layout, begins dynamic rendering, sets viewport/scissor, binds the sprite pipeline, binds vertex buffer slot 0 plus instance buffer slot 1, and records a direct `vkCmdDraw`.

The smoke test creates an offscreen `R8G8B8A8` target, upload-domain vertex and instance buffers, a readback buffer, sprite shaders, a sprite pipeline, records a full-screen quad draw, copies the image to readback memory, and verifies magenta RGBA bytes.

## Alternatives Considered

### Extend `VulkanDynamicRenderEncoder`

Rejected. That encoder proves generic dynamic rendering and indirect draw. Sprite rendering has a distinct vertex/instance binding contract and should not overload the generic path.

### Put instance data into descriptors for the first draw

Rejected for 12E. Stage 12D defines instance data as vertex-rate/instance-rate input. Descriptor-backed material and texture policy remains separate from proving the geometry/instance draw path.

### Make the smoke shader sample a texture immediately

Rejected. The immediate gate is real sprite vertex/instance submission. Texture atlas, sampler arrays, material selection, and descriptor update policy need their own integration stage.

## Consequences

- Stage 12 now has a real offscreen sprite draw proof.
- ECS still owns component streams and POD refs; encoder/runtime only resolve and record native work.
- Vertex and instance buffer binding now match the Stage 12D pipeline contract.
- Texture sampling and production material policy remain explicit future work.
