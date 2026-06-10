# ADR: Stage 13 Textured Sprite Sampling

Status: Accepted

Date: 2026-06-10

## Context

Stage 12 proved the real sprite vertex/instance draw path, but the smoke shader was color-only. The next integration step needs a true sampled image path without changing the ECS ownership model: components remain POD records, while Vulkan objects remain runtime-owned behind id + generation references.

## Decision

Add:

```text
include/Render2D/Native/VulkanSamplerRuntime.hpp
tests/vulkan_textured_sprite_render_test.cpp
```

Also extend:

```text
include/Render2D/Native/NativeComponents.hpp
include/Render2D/Native/VulkanResourceRuntime.hpp
tests/support/SpriteShaders.hpp
```

`SamplerRef<Provider, Dim>` is an ECS-visible Strict POD component containing a sampler handle snapshot, `sampler_id`, `generation`, and flags. `VulkanSamplerRuntime` owns the actual `VkSampler` slots, validates generations, rejects stale refs, reuses freed ids, and destroys samplers during release/shutdown.

`VulkanResourceRuntime::recordCopyBufferToImage` records tightly packed upload-buffer copies into managed images. It validates command buffer presence, live refs, transfer usage flags, supported 4-byte color formats, byte-count overflow, and source-buffer range.

The textured sprite smoke creates a 1x1 green texture, uploads it into a sampled image, updates the existing combined image sampler descriptor layout, renders a 4x4 offscreen sprite through `VulkanSpriteRenderEncoder`, copies the color target to readback memory, and verifies every pixel.

## Alternatives Considered

### Expose raw `VkSampler` to ECS

Rejected. ECS-facing resource records must use id + generation so stale references can be rejected and future host ECS migration stays data-oriented.

### Keep buffer-to-image copy as ad hoc test code

Rejected. Texture upload is a runtime resource operation and should share the same MemoryCenter-backed resource ownership and validation path as other Vulkan copies.

### Implement full atlas/material batching now

Rejected. Stage 13 only proves the sampled-image path. Atlas packing, material selection, texture arrays, and multi-texture batch policy require a separate design pass.

## Consequences

- Sprite rendering now has a real texture sampling smoke proof.
- `SamplerRef` joins the native POD reference set and can migrate directly into the host ECS.
- `VkSampler`, image, descriptor, pipeline, and command-buffer lifetimes remain runtime-owned.
- Production atlas/material policy and Vulkan text sampling remain future work.
