# ADR: Stage 12 Sprite GPU Instance Contract

Status: Accepted

Date: 2026-06-09

## Context

Stage 11 completed the native frame boundary. The next rendering milestone is a real sprite GPU path. Before adding upload buffers, descriptors, shaders, or pipeline state, Render2D needs ECS-visible POD records that describe the GPU-facing sprite instance stream.

## Decision

Add GPU-facing sprite component records in:

```text
include/Render2D/Component/Sprite.hpp
```

The new records are:

- `SpriteVertex`
- `SpriteInstance`
- `SpriteDrawPacket`

`SpriteInstance` stores a 2D affine transform as six floats, full-rect UV defaults, source identity, texture/material IDs, color, sort key, layer, and flags.

Add:

```text
include/Render2D/System/SpriteInstanceSystem.hpp
```

`SpriteInstanceBuildSystem` converts `DrawCommand[]`, `WorldTransform[]`, and `Sprite[]` into `SpriteInstance[]`. It writes each instance at `DrawCommand::instance_first` so sorted draw streams can keep stable references to precomputed instance slots.

## Alternatives Considered

### Store full `Mat3` in `SpriteInstance`

Rejected for this stage. A full 3x3 matrix carries an unused affine row for sprite rendering. Six explicit floats are smaller and closer to the GPU instance layout.

### Generate instance data inside Vulkan encoder code

Rejected. Instance data is ECS-visible derived component data. Vulkan code should upload and encode it, not hide a component transformation inside backend runtime code.

## Consequences

- Sprite GPU instance data can be migrated directly into the host ECS.
- The upload path can treat `SpriteInstance[]` as source component data.
- No Vulkan calls, runtime storage, or MemoryCenter allocations are introduced in 12A/12B.
- Future 12C-12E work can build upload buffers, descriptor layouts, pipelines, and offscreen sprite smoke on this stable component contract.
