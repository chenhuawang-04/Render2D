# ADR: Threaded CPU Pipeline Runtime

Status: Accepted

Date: 2026-06-09

## Context

Stage 10H needs to start using ThreadCenter for real CPU pipeline work without changing the ECS-facing data model.

Existing systems already provide the correctness reference:

- `TransformSystem`
- `BoundsSystem`
- `CullingSystem`
- `CommandBuildSystem`
- `BatchSystem`

The host engine will eventually own ECS storage, so Render2D cannot introduce production ECS storage or ThreadCenter-backed component ownership.

## Decision

Add `ThreadedCpuPipelineRuntime<Provider, Dim>` in:

```text
include/Render2D/System/ThreadedCpuPipeline.hpp
```

The runtime is a ThreadCenter-backed orchestration facade:

- inputs and outputs remain ordinary `std::span` component streams;
- Transform and Bounds write fixed contiguous chunks;
- Culling writes into per-chunk scratch backed by `Render2D::McVector`;
- visible items are merged in chunk order for deterministic output;
- CommandBuild runs in chunks over the merged visible stream;
- Batch remains single-threaded through `BatchSystem` for now.

The header is intentionally not part of `Render2D/Render2D.hpp`, because consumers must link the internal `render2d_thread_runtime_support` target to get ThreadCenter headers.

## Alternatives Considered

### Parallelize every stage immediately

Rejected for 10H. Parallel batching/sorting can change batch boundaries and needs a separate deterministic merge contract.

### Put ThreadCenter handles into components

Rejected. ThreadCenter remains runtime infrastructure only and must not enter ECS-visible component fields.

### Replace existing systems with threaded implementations

Rejected. The single-thread systems stay as the correctness reference and remain useful for small workloads, debugging, and deterministic comparisons.

## Consequences

- Sprite CPU path now has a real ThreadCenter execution path.
- ECS component contracts are unchanged.
- Render2D-owned threaded scratch uses `McVector`.
- Test coverage compares multi-worker and single-worker runtime output against the existing single-thread pipeline.
- Future work can extend the same pattern to text, sort, batch, and Vulkan command recording ownership.
