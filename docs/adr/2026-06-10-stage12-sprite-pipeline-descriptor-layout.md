# ADR: Stage 12 Sprite Pipeline and Descriptor Layout

Status: Accepted

Date: 2026-06-10

## Context

Stage 12C can upload `SpriteInstance[]` into GPU buffers. The next step needs a stable Vulkan pipeline and descriptor contract so 12E can perform a real offscreen sprite draw without inventing descriptor ownership or hiding ECS data inside runtime storage.

## Decision

Extend `VulkanGraphicsPipelineConfig` with optional vertex binding and vertex attribute descriptions. Existing callers may pass null pointers and zero counts; sprite creation supplies a concrete layout.

Add:

```text
include/Render2D/Native/VulkanSpritePipelineRuntime.hpp
```

The new stateless helper defines:

- binding 0: `SpriteVertex`, vertex-rate;
- binding 1: `SpriteInstance`, instance-rate;
- attributes for position, UV, transform row 0, transform row 1, UV rect, and packed RGBA8 color;
- a descriptor runtime config helper for combined image sampler descriptors;
- a sprite pipeline creation helper that calls `VulkanPipelineRuntime`.

Sprite instance source/material/texture metadata remains in the ECS component stream. It is not exposed as shader-visible vertex input in this stage.

## Alternatives Considered

### Put sprite instance data in a storage-buffer descriptor

Rejected for 12D. Stage 12C already uploads instance data to a GPU buffer, and the immediate 12E draw can use vertex-instance binding directly. Storage-buffer indexing can be added later if batching requires it.

### Add sprite-specific state to `VulkanPipelineRuntime`

Rejected. The generic runtime should remain backend lifecycle infrastructure. Sprite-specific layout policy belongs in a stateless helper layered above it.

### Include material/texture IDs as vertex attributes immediately

Rejected for this stage. Descriptor and material selection still need a batch-level policy. Keeping metadata in components avoids freezing a shader ABI too early.

## Consequences

- Sprite pipeline creation has a stable vertex/instance input contract.
- Descriptor layout starts with combined image samplers only.
- Runtime remains ECS-storage-free and resolves `PipelineRef` / `DescriptorSlice` by id + generation.
- 12E can focus on binding buffers/descriptors and issuing a real offscreen sprite draw.
