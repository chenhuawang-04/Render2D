# ADR: Stage 20 Bindless Descriptor Indexing

Date: 2026-06-13

## Status

Accepted. Stage 20 (20A–20F) is closed. The bindless path ships behind a
capability probe and is proven byte-equal to the existing combined-image-sampler
(CIS) path; the CIS path remains the fallback reference and is unchanged.

## Context

Through Stage 16 the sprite path bound **one descriptor set per texture**: the
encoder rebinds whenever a packet's descriptor changes (`VulkanSpriteRenderEncoder::recordPackets`,
`!isSameDescriptor`), and `BatchSystem` keys batches on `texture_id`, so two
draws that differ only by texture can never merge. That is correct but caps the
batch merge rate and the descriptor-bind count scales with distinct textures per
frame.

Stage 20 adds a **bindless** alternative: a single descriptor set holds every
texture in an array indexed per-instance in the shader, so draws differing only
by texture merge into one batch and one packet, bound once per frame. The work
was staged so the CPU systems, the table runtime, and the render proof could each
be verified independently before any of them touched the proven CIS contracts.
This ADR records the four design decisions those sub-stages settled, all now
backed by compiling, GPU-verified code.

## Decision

### 1. Split descriptor set, not a combined-image-sampler array (20B)

The bindless table (`VulkanBindlessTextureTable<Provider, Dim>`) **owns its own
descriptor set** with two bindings:

- binding 0 = `SAMPLED_IMAGE[]` — the texture array, sized once to
  `texture_capacity` (clamped to `capability.max_descriptor_set_sampled_images`),
  flagged `PARTIALLY_BOUND | UPDATE_AFTER_BIND`;
- binding 1 = `SAMPLER[]` — a small sampler array, written once (no UAB).

It deliberately does **not** reuse `VulkanDescriptorRuntime`'s combined-image-sampler
array. Separating images from samplers is what lets a sampler be chosen
per-instance independently of the texture (decision 2), and it keeps the table a
self-contained owner of its Vulkan objects — the same "each runtime owns its
handles" pattern as every other native runtime. `VulkanDescriptorRuntime` is left
untouched as the non-bindless fallback. On a non-bindless device the table's
`initialize` returns `UnsupportedDomain` and the caller keeps the CIS path.

### 2. The sampler is a material property (20D)

`SpriteInstance` and `SpriteMaterialBinding` each gained a trailing `U32
sampler_index` (trailing keeps the field additions POD-safe and leaves vertex
attribute offsets stable). The pure pass `SpriteInstanceBuildSystem::resolveSamplerIndices`
stamps `sampler_index` from a `material_id`+generation lookup (missing/stale →
`InvalidInput`, never a silent slot 0). The bindless fragment shader samples
`sampler2D(textures[nonuniformEXT(in_texture_id)], samplers[nonuniformEXT(in_sampler_index)])`.
The sampler is therefore selected by material, not hardcoded, and travels with
the instance through the same per-instance attribute channel as the texture id.

### 3. Identity texture indexing; host-supplied generation; CPU-only stale gate (20B/20C)

The shader indexes binding 0 **directly by `texture_id`** with no indirection
table — texture ids are dense by contract. `setResident`/`evict`/`isResident`
key on `texture_id`+generation, but the **generation is host-supplied, not
table-owned**, and the CPU stale gate is the *only* place generation is ever
checked: a stale `id + generation` is rejected on the CPU before it can reach a
draw. The shader sees the id alone. `BatchSystem::runBindless` and
`makeBindlessDrawSortKey` zero the texture bits so texture-only differences merge,
while still comparing material id/generation, sort key, layer, and flags — a
stale material generation can never be merged away.

### 4. Partially-bound with no backfill (20B)

The texture array is pure `PARTIALLY_BOUND` with **no default backfill** — a
large `texture_capacity` reservation is nearly free and unbound slots cost no
image. This is a deliberate perf+space choice over filling every slot with a
default view. A Debug-only `debug_fill_view` (1×1 magenta, under `#ifndef NDEBUG`)
is available to make sampling an unbound slot visually obvious during development;
it is zero-cost in Release. The only `McVector` in the table is the CPU residency
record (`slots`, lazily grown, reserved to capacity); the GPU array is a
fixed-count Vulkan binding sized once and never a growable vector.

### 5. Encoder binds the table's set once per frame (20E)

`VulkanSpriteRenderEncoder::recordBindless` parallels `recordPackets` but takes
the `VulkanBindlessTextureTable` instead of a descriptor runtime: it validates
`isInitialized()`, binds the table's self-owned single descriptor set **once per
frame** (rebound on each pipeline bind to survive a layout change), and per packet
only rebinds the pipeline on change then `vkCmdDraw` — it **never binds a
per-packet descriptor**, and the packet's `texture_id`/`descriptor_*` fields are
ignored on this path. `record`/`recordPackets` are untouched.

## Consequences

- The bindless and CIS paths render the same scene byte-for-byte. The 20E proof
  (`tests/vulkan_bindless_sprite_render_test.cpp`) renders an 8-band scene — 8
  distinct 1×1 textures, 2 samplers, per-instance `texture_id`/`sampler_index =
  band % 2`, **one packet over all 8 instances** — through `recordBindless`, then
  the same scene through the proven CIS `recordPackets` (one descriptor per
  texture) in the *same* command buffer, and asserts the two readbacks
  `memcmp`-equal **and** each band shows its own texture color (the per-band check
  defeats a mutual-failure match).
- **Equivalence boundary (honest):** with 1×1 textures, NEAREST filtering and the
  second sampler (which differs only in address mode) are byte-identical, so the
  equivalence is insensitive to sampler *filtering*. What is proven is that a
  single set renders multiple textures and multiple samplers correctly and matches
  the fallback, with both sampler-array entries genuinely exercised — not that
  filtering differences round-trip.
- The bindless path is fully gated: `queryVulkanBindlessCapability` (20A) requires
  all four descriptor-indexing features (partiallyBound, runtimeDescriptorArray,
  shaderSampledImageArrayNonUniformIndexing, descriptorBindingSampledImageUpdateAfterBind);
  absent any of them, the table refuses to initialize and the caller stays on the
  unchanged CIS path. No existing contract regresses.
- The bindless shaders target `vulkan1.2` (`kSpriteBindlessVertSpv`/`kSpriteBindlessFragSpv`)
  and use `nonuniformEXT` for both the texture and sampler index;
  `VulkanSpritePipelineRuntime::createBindlessPipelineRef` wires the 8-attribute
  vertex layout (locations 6/7 `R32_UINT` for texture id / sampler index).
- `VulkanSpriteMaterialGraph` (20D, CPU-only, no Vulkan handle) maps identity
  `material_id`+generation → `{PipelineRef, sampler_index, VulkanSpriteMaterialParams}`,
  generation-validated. It is the material source for `resolveSamplerIndices`.
- Architecture red lines hold: components stay Strict POD (trailing `U32`
  additions only), the only dynamic array in the table is a `McVector` residency
  record, no direct Vulkan memory API is introduced, refs remain `id + generation`,
  and the pure systems (`BatchSystem`, `SpriteDrawPacketBuildSystem`,
  `SpriteInstanceBuildSystem`) stay Vulkan-free and deterministic.
- Deferred beyond Stage 20: production sampler-filtering equivalence (textures
  larger than 1×1), residency eviction policy under capacity pressure, and a
  material graph that drives more than sampler/pipeline selection. These are
  non-blocking host/runtime refinements.
