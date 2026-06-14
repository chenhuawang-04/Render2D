# ADR: Parallel Deterministic Draw Sort

Status: Accepted

Date: 2026-06-15

## Context

Stage 21B parallelizes the sorted-path tail. The Threaded CPU Pipeline Runtime ADR
(2026-06-09) deliberately left batching and sorting single-threaded, noting that
"parallel batching/sorting can change batch boundaries and needs a separate
deterministic merge contract." This ADR is that contract for the sort.

The sorted tail is two stages: `DrawSortSystem` (a stable 4-pass LSD radix sort over
`DrawCommand.sort_key`) followed by `BatchSystem` (an adjacent-merge scan). The Stage
10F sorted capture in `BENCHMARK_BASELINE.md` is the data: at 10–12k draws the sort is
`0.18–0.19 ms` — the single largest stage in the sorted path (transform `0.09`, bounds
`0.04`, batch `0.02`) — and it grows with draw count. `BatchSystem` is both tiny
(`~0.02 ms`) and inherently sequential (it carries a running batch index across
adjacent draws).

A non-negotiable repo invariant constrains any parallel path: multi-threaded output
must be **byte-identical** to the single-thread reference (`ProjectMergeTODO.md`;
ReinforcementPlan "并行确定性"). For a sort this is strictly stronger than "correctly
ordered" — equal-key draws must keep their exact original relative order, because the
downstream `BatchSystem` merge and the GPU draw order both depend on the precise
permutation.

## Decision

Add `ThreadedDrawSortRuntime<Provider, Dim>` in:

```text
include/Render2D/System/ThreadedDrawSort.hpp
```

It is a ThreadCenter-backed orchestration facade, mirroring
`ThreadedCpuPipelineRuntime`:

- inputs/outputs remain ordinary `std::span` component streams; the caller owns the
  two `SortedItem` scratch buffers, exactly as `DrawSortSystem::run` requires;
- the draws are partitioned into contiguous chunks (`min_items_per_task`, capped by
  worker count); per-chunk histogram/offset scratch lives in runtime-owned
  `Render2D::McVector` members (resized, then reused across frames);
- each of the 4 radix passes runs `histogram (parallel per chunk) → offset (single
  task) → scatter (parallel per chunk)`, expressed as a ThreadCenter plan with a
  `precede` dependency chain, followed by a final parallel gather;
- gated by the shared Stage 21E threshold (`ParallelPolicy.hpp`): sub-threshold or
  single-worker workloads route to `DrawSortSystem::run` unchanged;
- **only the radix sort is parallelized**; `BatchSystem` stays single-threaded.

The header is intentionally **not** part of `Render2D/Render2D.hpp`: consumers must
link the internal `render2d_thread_runtime_support` target to get ThreadCenter
headers, the same isolation as the other threaded runtimes.

### The determinism contract

The serial radix is stable: within each radix bucket, items retain increasing
source-index order. The parallel form reproduces that exact permutation by having the
single-task offset pass compute the global scatter position for every `(bucket,
chunk)` in **bucket-major, then chunk order**:

```
running = 0
for bucket in 0..256:           # bucket-major outer
    for chunk in 0..N:          # chunk-order inner
        offset[chunk][bucket] = running
        running += histogram[chunk][bucket]
```

So all of bucket *b*'s items land before bucket *b+1*'s, and within a bucket chunk
0's items land before chunk 1's. Because chunks are contiguous slices of the source
array, "chunk order" is "source order", which reproduces the serial radix's
increasing-source-index order per bucket — the identical stable permutation. Each
chunk then scatters into its own disjoint `offset[chunk][*]` cursors, so writes never
overlap and need no synchronization. Verified by `render2d.threaded_draw_sort` (a
`memcmp` equivalence test over a heavily key-colliding stream split across uneven
chunks) and by a per-frame `memcmp` in `render2d_threaded_draw_sort_bench`.

## Alternatives Considered

### Also parallelize `BatchSystem`

Rejected. It is measured tiny and is an inherently sequential running-index merge
scan; there is no data-backed reason to pay ThreadCenter overhead, and a parallel
merge would need its own (harder) deterministic-boundary contract for no measured win.

### Unstable parallel sort, or atomic bump-scatter into shared buckets

Rejected. Both reorder equal-key draws relative to the serial output, which breaks the
byte-equality invariant and changes downstream batch boundaries and GPU draw order.
The whole point of the bucket-major/chunk-order offset is to avoid this.

### `std::stable_sort` / `std::sort` with a key comparator, run in parallel

Rejected. Even a stable comparator sort is not guaranteed to yield the byte-identical
permutation the serial radix produces for equal keys, and it would diverge from the
established `DrawSortSystem` reference rather than reuse it. The radix is also faster
at the draw counts where parallelism pays off.

### Chunk-major-then-bucket offset ordering

Rejected. Ordering offsets by chunk first would place chunk 0's bucket-1 items before
chunk 1's bucket-0 items, i.e. not globally bucket-ordered — wrong sort result, not
just a different tie-break.

## Consequences

- The sorted tail now has a ThreadCenter execution path that is byte-identical to the
  single-thread reference; `DrawSortSystem` remains the correctness reference and the
  sub-threshold path.
- ECS component contracts are unchanged; the runtime consumes/produces `std::span`.
- Render2D-owned threaded scratch uses `McVector` (histograms/offsets/chunk table),
  consistent with the existing threaded runtime; the caller still owns the
  `SortedItem` scratch spans.
- Speedup scales with draw count and plateaus near `~2.4x` at 4 workers / `~3.2x` at
  20 (see `BENCHMARK_BASELINE.md`). The ceiling is inherent to a parallel radix: the
  per-pass offset is a single serial prefix-sum task and the seed/gather passes add
  bandwidth-bound overhead the serial sort does not pay. This is an expected Amdahl
  limit, not a defect; parallelizing the prefix-sum is possible but not justified by
  current data.
- Future work can apply the same per-pass-plan pattern to other order-preserving
  stream transforms, and revisit `BatchSystem` only if a workload makes the merge hot.
