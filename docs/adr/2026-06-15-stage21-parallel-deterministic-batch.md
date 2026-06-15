# ADR: Parallel Deterministic Batch Merge

Status: Accepted

Date: 2026-06-15

## Context

Stage 21B parallelized the draw sort but deliberately left `BatchSystem`
single-threaded. Its ADR
(`2026-06-15-stage21-parallel-deterministic-draw-sort.md`) recorded the reason —
batch was "both tiny (`~0.02 ms`) and inherently sequential" at the 10–12k draws
of the Stage 10F capture — and its Consequences left an explicit door open:
"revisit `BatchSystem` only if a workload makes the merge hot."

A Stage 21 at-scale sweep (recorded in `BENCHMARK_BASELINE.md`) made it hot. The
`~0.02 ms` reading was a small-scale artifact: the batch scan is `5.3 ms` at 1M
draws and `10–15 ms` at 2M, comparable to every other per-element sprite stage.
`BatchSystem` is a single O(n) pass over 56-byte `DrawCommand`s, so it is
partly memory-bandwidth-bound — but the dominant cost on a realistic sorted or
clustered stream is the *start-finding key comparison* scan, which parallelizes
cleanly; only a degenerate stream where no two adjacent draws merge is
write-bound. A measurement spike confirmed this: at 20 workers the parallel form
ran `1.6x` on the degenerate no-merge stream rising to `4.2x` on the
sorted/clustered stream (few long runs), byte-identical to the serial scan every
frame.

The same non-negotiable invariant as the sort applies: the multi-threaded output
must be **byte-identical** to the single-thread reference (`ProjectMergeTODO.md`;
ReinforcementPlan "并行确定性"), because the resulting `BatchCommand` stream drives
native submit order.

## Decision

Add `ThreadedBatchRuntime<Provider, Dim>` in:

```text
include/Render2D/System/ThreadedBatchSystem.hpp
```

It is a standalone ThreadCenter-backed orchestration facade, mirroring
`ThreadedDrawSortRuntime` (one deterministic runtime per parallelized stage):

- `runBatch` / `runBatchBindless` are the parallel equivalents of
  `BatchSystem::run` / `runBindless`; inputs/outputs stay ordinary `std::span`
  component streams;
- the draws are partitioned into contiguous chunks (`min_items_per_task`, capped
  by worker count); per-chunk and `starts` scratch live in runtime-owned
  `Render2D::McVector` members, resized then reused across frames;
- gated by the shared Stage 21E threshold (`ParallelPolicy.hpp`): sub-threshold or
  single-worker workloads route to `BatchSystem::run` / `runBindless` unchanged;
- a **full-capacity contract** — the batch stream must have capacity `>=` the draw
  count (a batch stream holds at most one batch per draw). This lets the parallel
  scatter write without a per-element bounds check; smaller buffers are rejected
  with `InsufficientCapacity` up front, before any parallel work. The
  single-thread reference still accepts smaller buffers.

`makeBatch` is replicated in the runtime (not shared from `BatchSystem`, whose
copy stays the pristine reference); the byte-equality test fails immediately if
the two ever drift. This mirrors how Stage 21B reimplemented the radix rather
than sharing `DrawSortSystem`'s loop.

The header is intentionally **not** part of `Render2D/Render2D.hpp`: consumers
link the internal `render2d_thread_runtime_support` target, the same isolation as
the other threaded runtimes.

### The determinism contract

A batch is a maximal run of adjacent draws with an equal batch key. Define a
"start" at index `i` as `(i == 0) || !equalBatchKey(draw[i-1], draw[i])`. The
start predicate is **chunk-independent**: a chunk's first index reads the previous
draw across the boundary read-only, so the set of starts — and therefore the set
of batches and their fields, all taken from the start draw — is identical no
matter how the stream is partitioned. Each batch begins at a start and runs to the
next start, which may cross chunk boundaries (a single run can span several
chunks).

The parallel form is three stages on one plan, `precede`-chained:

```
find   (parallel): per chunk, scan its draws and write the start indices densely
                   into starts[first .. first + count) (its own slice).
offset (serial)  : prefix-sum the per-chunk start counts into global batch
                   offsets; then a right-to-left pass records, for each chunk, the
                   first start strictly after it (or n) so the chunk's last batch
                   knows where its run ends across later start-free chunks.
write  (parallel): per chunk, emit its batches at its global offset; each batch's
                   draw_count is the gap to the next start (the next start in the
                   chunk, or the chunk's "first start after" for its last batch).
```

Chunks write disjoint output ranges and read the draws/starts read-only, so the
result is byte-identical to `BatchSystem`. Verified by
`render2d.threaded_batch_system` (a `memcmp` equivalence test over singleton,
chunk-crossing, whole-stream-single-run, and bindless fills across an uneven
chunk split) and by a per-frame `memcmp` in `render2d_threaded_batch_bench`.

## Alternatives Considered

### Reuse `BatchSystem::run` per chunk, then a serial boundary merge

Rejected. Running the reference per chunk and stitching boundaries needs a serial
compaction pass over the *batches* to drop continuation fragments and fix counts —
`O(batch_count)`, which is `O(n)` on a no-merge stream — capping the achievable
speedup. The start-based form keeps the serial stage `O(chunk_count)` (a few
dozen entries) and parallelizes both heavy passes (scan and write).

### Merge sort + batch into one `ThreadedSortedTail` runtime

Rejected for now. A combined runtime would share one executor and chunk scratch
across the sorted tail (`sort -> batch`), which is more efficient, but it would
rewrite the Stage 21B sort runtime and couple two stages. Keeping one
deterministic runtime per stage matches the established pattern
(`ThreadedCpuPipeline`, `ThreadedTextCpuPipeline`, `ThreadedDrawSort`) and leaves
the proven sort runtime untouched. Sharing a single executor across the tail is a
host orchestration concern to settle at merge, not here.

### Atomic running-index / unstable merge

N/A. Any reordering of equal-key draws or non-deterministic batch boundaries
breaks the byte-equality invariant and changes native submit order.

### Keep `BatchSystem` single-threaded

Rejected. That was the right call at 10–12k draws, but the at-scale data shows a
real `3–4x` win at 1–2M draws on the sorted/clustered stream where batching is
actually used. The threshold gate preserves the single-thread path for small
workloads.

## Consequences

- The sorted tail (`sort` then `batch`) now both have ThreadCenter execution paths
  byte-identical to their single-thread references; `BatchSystem` remains the
  correctness reference and the sub-threshold path.
- ECS component contracts are unchanged; the runtime consumes/produces
  `std::span`.
- Render2D-owned threaded scratch uses `McVector` (`starts` sized to the draw
  count, plus three per-chunk arrays); the single-thread reference needs none.
- Speedup depends on run structure: a degenerate no-merge stream is write-bound
  (`~1.6x` at 20 workers, the `BatchCommand` writes are bandwidth-bound), while
  the realistic sorted/clustered stream is scan-bound and reaches `3–4x` (see
  `BENCHMARK_BASELINE.md`). This split is expected, not a defect.
- A host that uses `ThreadedDrawSortRuntime` and `ThreadedBatchRuntime` together
  owns two ThreadCenter executors (two pools); collapsing the sorted tail onto a
  single shared executor is a host orchestration optimization deferred to merge.
- The Stage 21 perf gate is unaffected: batch counts are deterministic and the
  parallel output is byte-identical, so the frozen `--expect-batches` values are
  unchanged.
