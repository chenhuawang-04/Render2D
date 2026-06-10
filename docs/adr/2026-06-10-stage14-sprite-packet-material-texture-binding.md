# ADR: Stage 14 Sprite Packet Material and Texture Binding

Status: Accepted

Date: 2026-06-10

## Context

Stage 13 proved a real sampled sprite path, but it still rendered one texture/descriptor batch. Production sprite rendering needs a component-level handoff from sorted batches to GPU draw packets without moving material, texture, pipeline, or descriptor ownership into Render2D ECS storage.

## Decision

Extend sprite resource identity records so texture and material ids carry generation values through `Sprite`, `DrawCommand`, `BatchCommand`, `SpriteInstance`, and `SpriteDrawPacket`.

Add POD binding streams:

```text
SpriteMaterialBinding[]
SpriteTextureBinding[]
```

`SpriteMaterialBinding` maps material id + generation to pipeline id + generation. `SpriteTextureBinding` maps texture id + generation to descriptor id + generation and descriptor range.

Add `SpriteDrawPacketBuildSystem`, which converts:

```text
BatchCommand[] + DrawCommand[] + SpriteMaterialBinding[] + SpriteTextureBinding[]
    -> SpriteDrawPacket[]
```

The first implementation emits one packet per batch and requires the batch's instance range to be contiguous before using one instanced draw. This keeps the GPU command contract explicit and avoids silently drawing unrelated instance slots.

Extend `VulkanSpriteRenderEncoder` with `recordPackets`. It resolves pipeline and descriptor refs from packets, binds vertex/instance buffers once, then draws all packets in one dynamic rendering pass, rebinding pipeline/descriptor only when the packet identity changes.

## Alternatives Considered

### Keep texture/material as id-only

Rejected. Resource ids can be reused. Generation fields are required to prevent stale ECS records from merging or resolving to the wrong runtime object.

### Let the encoder derive bindings from `BatchCommand`

Rejected. The encoder is runtime-only and should not own material/texture policy. A POD packet stream keeps host ECS integration explicit.

### Support non-contiguous instance batches immediately

Rejected for Stage 14. Splitting non-contiguous batches into multiple packets or compacting instances requires a separate batching policy. The current system rejects non-contiguous instance ranges instead of recording incorrect draws.

## Consequences

- Multi-texture sprite rendering is now represented as ECS-visible packet streams.
- Runtime still owns Vulkan pipeline/descriptor lifetimes and only resolves id + generation references.
- Basic multi-packet rendering is proven by a red/green offscreen smoke test.
- Atlas packing, complex material graphs, bindless descriptor indexing, and Vulkan text draw integration remain future work.
