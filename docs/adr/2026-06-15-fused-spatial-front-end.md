# ADR: Fused Spatial Front-End (transform → bounds → culling)

Status: Accepted

Date: 2026-06-15

## Context

The Stage 21 at-scale profile (`BENCHMARK_BASELINE.md`) classified the sprite
front-end — `TransformSystem → BoundsSystem → CullingSystem` — as
**bandwidth-bound** in the common static case: threading the whole pipeline
plateaued near `1.3x` and 20 workers ≈ 4 workers, the memory-wall signature.
Each element is read and rewritten three times across the three passes, and the
intermediate `WorldTransform` (`40 B`) and `WorldBounds` (`20 B`) arrays are each
written once and re-read once between stages.

Stage 24 Track 1 set out to collapse those three passes into one, computing the
world affine and world AABB once while the element is hot in cache and skipping
the intermediate-array traffic.

**A code finding corrected the original Track 1 sketch.** The plan assumed the
fused pass could drop *both* `WorldTransform` and `WorldBounds`
(`~190 B → ~65 B`). But `SpriteInstanceBuildSystem::run` reads
`world_transforms_[source_index].affine` downstream (see `SpriteInstanceSystem`),
so the dense `WorldTransform` array is a **real output** that must persist.
`WorldBounds`, by contrast, is consumed *only* by the cull test, so it is
genuinely transient. The fused pass therefore still writes `WorldTransform` and
drops only `WorldBounds` materialization — a narrower but still substantial
traffic cut.

The same non-negotiable invariant as the other Stage 21/24 parallel work applies:
the fused output must be **byte-identical** to the three-system reference
(`ProjectMergeTODO.md`; ReinforcementPlan "并行确定性"), because `WorldTransform`
feeds the sprite-instance affine and `VisibleItem` drives draw order.

## Decision

Add `SpatialCullSystem<Provider, Dim>` in:

```text
include/Render2D/System/SpatialCullSystem.hpp
```

A pure, dependency-free system (in the umbrella `Render2D.hpp` alongside the other
deterministic systems), single `run`:

```text
run(camera, transforms, local_bounds, visibility_masks,
    out world_transforms, out visible_items)
```

In one pass it: writes `world_transforms[i]` for every element, computes the world
AABB in a register, applies the cull test, and emits `VisibleItem`s. `WorldBounds`
is never stored. The per-element affine math is replicated verbatim from
`TransformSystem::writeWorldTransform` (factored to return the `Mat3`), the bounds
math from `BoundsSystem::transformBounds`, and the cull decision from
`CullingSystem::run` (same camera bounds, same mask/intersect test, same
`VisibleItem` fields and emission order). The three granular systems are left
**untouched** as the deterministic reference; `tests/spatial_cull_system_test.cpp`
`memcmp`-verifies the fused output against the chain.

The affine is computed into a **local** `Mat3`, stored to `world_transforms[i]`,
and the *local* (not a re-read of the just-stored memory) is passed to the bounds
math. This removes a store-to-load dependency that measurably hurt the
compute-bound rotating path; the stored and reused bits are identical, so
byte-identity holds.

`ThreadedCpuPipelineRuntime` is rewired to use it: the former three-stage
`transform → bounds → culling` `precede` chain (three dispatched `parallelFor`s
over the whole array, plus a materialized `WorldBounds`) becomes a **single
`parallelFor`** running `SpatialCullSystem` per chunk, then the existing
chunk-local `source_index/sort_key` offset and `mergeVisibleItems` compaction. The
single-thread reference path (`runSingleThreadedSpritePipeline`) uses the same
fused system, so all three forms — single-thread reference, sub-threshold gate,
and chunked parallel — agree byte-for-byte.

`runSpritePipeline`'s public signature is unchanged. The `world_bounds_` span is
retained (and its capacity still validated) for API stability and as caller-owned
scratch, but is **no longer written**. This is a deliberate, documented wart: a
caller that previously read back `world_bounds` from this runtime no longer can
(nothing in-repo or in the merge contract does — `WorldBounds` was never consumed
past culling).

### The determinism contract

Per element `i`, the cull decision depends only on `camera`, `visibility_masks[i]`,
and the world AABB derived from `transforms[i]` and `local_bounds[i]`. The fused
pass feeds those byte-identical inputs to the same replicated math, so:

- `world_transforms` matches `TransformSystem::run` exactly (the affine is written
  for every element regardless of visibility);
- the world AABB bits match what `BoundsSystem` would have stored, so the
  `aabb2Intersects` result — and thus the visible set, order, and `VisibleItem`
  fields — match `CullingSystem::run`.

Skipping the bounds math for mask-failed elements is safe: such elements are never
visible in either form, and `WorldBounds` is not an output. In the chunked path
each chunk processes a contiguous range over disjoint output slices and the
per-chunk visible items are globalized (offset by `chunk.first`) and merged in
chunk order — identical to a single pass. On the x64/SSE target there is no
excess-precision divergence between the register-only world AABB and the
reference's memory round-trip (all math is `float`-typed).

## Alternatives Considered

### Drop `WorldTransform` too (the original `~190 B → ~65 B` sketch)

Rejected: `SpriteInstanceBuildSystem` reads the dense affine downstream. Dropping
it would force a recompute (another sincos pass) at instance-build time or a
signature change to that system — out of Track 1 scope and a net loss on the
rotating path. Writing `WorldTransform` and dropping only `WorldBounds` keeps the
contract and still removes the `WorldBounds` write + both inter-stage re-reads.

### Refactor the three granular systems to share the per-element helpers

Rejected. Extracting shared helpers would make byte-identity structural rather than
tested, but it touches the deterministic reference systems. The repo's established
pattern (Stage 21B replicated `makeBatch`; the draw-sort runtime reimplemented the
radix) is to replicate and guard with a `memcmp` equivalence test, leaving the
reference pristine. Followed here.

### Keep the three-stage threaded chain

Rejected. The chain's three dispatched stages and materialized `WorldBounds` are
exactly the inter-stage traffic the fusion removes. The fused single pass is a
clear win on the bandwidth-bound static case and neutral on the compute case (see
Consequences); the threshold gate still routes small workloads to the single-thread
fused path.

## Consequences

- **Measured (single-thread, `spatial_cull_bench`, RelWithDebInfo):**
  - Static / bandwidth-bound: **`1.5x`–`1.6x`** at 1M–2M sprites (high
    visibility), rising to **`~2.2x`** on the cull-heavy low-visibility stream
    (fused skips the bounds math for mask-failed elements and drops their traffic).
  - Rotating / compute-bound: **`~1.0x` (neutral, `0.94x`–`1.08x` run-to-run)** —
    `MMath::sincos` dominates (`~44 ms`/1M) and swamps the bandwidth savings.
    Fusion neither helps nor meaningfully hurts here; **Stage 24 Track 2** (batched
    SIMD `sincos` in fast_math) is the lever for this path.
  - The fusion wins exactly where the Stage 21 profile said the bottleneck is
    (bandwidth), and is harmless where it said the bottleneck is compute.
- Byte-identical to the `TransformSystem → BoundsSystem → CullingSystem` chain;
  verified by `render2d.spatial_cull_system` (`memcmp` of `world_transforms` +
  `visible_items` across rotation, masking, and capacity cases),
  `render2d.threaded_cpu_pipeline` (now also asserts the threaded affine exactly),
  and a per-frame `memcmp` in `render2d_spatial_cull_bench`.
- ECS component contracts unchanged; the system consumes/produces `std::span`. The
  granular `TransformSystem`/`BoundsSystem`/`CullingSystem` are untouched and remain
  the reference.
- `ThreadedCpuPipelineRuntime` now dispatches one stage instead of three for the
  front-end (one `parallelFor`, one `wait`/`waitIdle` instead of a three-task
  `precede` chain), removing per-frame scheduling overhead on top of the traffic
  cut. Its `world_bounds_` output is no longer populated (documented above).
- The Stage 21 perf gate is unaffected: visible/draw/batch counts are deterministic
  and the parallel output is byte-identical, so the frozen expectations hold (Perf
  `76/76`, gate green).
