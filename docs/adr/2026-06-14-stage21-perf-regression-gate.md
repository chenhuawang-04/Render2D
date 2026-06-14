# ADR: Stage 21 (pre-work) — Automated Performance-Regression Gate

Date: 2026-06-14

## Status

Accepted. A Stage 21 prerequisite landed ahead of the parallelization work
(21A/21B) so those changes have an automated guard from the first commit. It
promotes the manual "Gate Rule" in `docs/architecture/BENCHMARK_BASELINE.md`
into CTest cases; it supersedes nothing.

No ECS/system/runtime contract changed: systems still take `std::span`,
components stay Strict POD, refs remain `id + generation`, and the umbrella
`Render2D.hpp` is untouched. Only the benchmark harness and the test registration
changed.

## Context

The project had a real CPU benchmark harness (`bench/null_cpu_bench.cpp` +
`bench/support/BenchmarkFramework.hpp`), scripted suites, and a manually-recorded
baseline, but **no automated perf-regression gate**: nothing failed CI when a
number got worse. `ProjectMergeTODO.md` #22 also lists threshold gating as open.

The harness emits two classes of output, with very different reliability:

- **Work counts** (visible / total draws / glyph draws / batches) are produced by
  deterministic, allocation-free systems and are **identical on every machine and
  every run**. They are the precise detectors of *algorithmic* regressions: if
  culling stops culling, `visible` jumps; if batch-merge breaks, `batches` jumps;
  if sort breaks, the sorted-scenario `batches` stops collapsing.
- **Per-stage wall-clock** is machine-dependent and noisy on shared CI runners,
  where a tight absolute budget would flake constantly.

A gate that keys primarily on wall-clock would be either flaky (tight) or useless
(loose). The honest design keys the *hard* gate on the deterministic counts and
keeps wall-clock as a *generous* secondary guard.

## Decision

Add a perf-gate mode to the harness (`GateSpec`, gate flags in
`parseBenchmarkConfig`, `checkGate`). After the normal report, `null_cpu_bench`
checks any expectation flags present and exits non-zero on violation. Two layers:

1. **Deterministic work-count expectations** (`--expect-visible`,
   `--expect-total-draws`, `--expect-glyph-draws`, `--expect-batches`). Always on,
   machine-independent, never flaky. This is the real regression net.
2. **Generous wall-clock catastrophe budget** (`--max-total-avg-ms`, cache var
   `RENDER2D_PERF_GATE_MAX_TOTAL_AVG_MS`, default `25`). Asserts summed per-frame
   stage time stays under a budget set ~80x above the local Perf baseline. It trips
   on O(n^2)-class slowdowns and hot-loop allocation (hundreds of ms at 10k items
   even optimized), not on normal CI variance.

Registered as 7 `render2d.perf_gate_*` CTest cases in `bench/CMakeLists.txt`,
covering sprite (high/low/sorted), text (plain/sorted), and mixed (plain/sorted).
Because benchmarks are OFF in the Debug preset, the gate builds and runs **only**
under `clang-ninja-perf` (optimized RelWithDebInfo) and rides the existing CI
`Test (Perf)` step — no workflow change. Gate flags are optional, so all existing
bench invocations and smoke cases are unaffected.

## Consequences

- A regression that changes pipeline output structure, or a catastrophic slowdown,
  now fails CI automatically and deterministically, with a `perf-gate: FAIL ...`
  diagnostic naming the metric, expected, and actual.
- **Honest limitation:** a green gate proves "correct output structure" and "no
  catastrophic slowdown" — it does **not** prove "no small regression". Micro-
  regressions (e.g. a 2x stage slowdown still well under 25 ms) still require the
  manual before/after capture in `BENCHMARK_BASELINE.md`. This is by design: a gate
  tight enough to catch micro-regressions would flake on shared runners.
- The expected counts are frozen in two places — `bench/CMakeLists.txt` and the
  table in `BENCHMARK_BASELINE.md`. A future stage that *intentionally* changes a
  count (e.g. parallel batching that alters the batch total) must update both in the
  same commit, with the before/after recorded — turning an unexplained count change
  into an explicit, reviewed decision.
- The gate runs only on the Perf preset, so it does not slow the Debug inner loop.

## Verification

- Perf `ctest` 68/68 (61 prior + 7 gate cases); each gate case ~0.01 s, far under
  the 25 ms budget. Debug `ctest` 52/52 unaffected (bench not built in Debug).
- Constraint scan 3/3, `clang-tidy --verify-config` clean, `git diff --check` clean.
- Fault-injection: a wrong `--expect-batches`, a wrong `--expect-visible`, and an
  impossibly tight `--max-total-avg-ms` each produced the expected `perf-gate: FAIL`
  line and exit code 2.
