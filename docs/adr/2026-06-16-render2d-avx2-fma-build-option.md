# ADR: AVX2+FMA build option and the batched transform path (Stage 24 Track 2)

Status: Accepted

Date: 2026-06-16

## Context

Stage 21 classified the **rotating** transform path as **compute-bound**: each
`TransformSystem` element evaluates `MMath::sincos`, and at scale the scalar trig
dominated (the at-scale profile measured ~41 ms for 1M rotating world-transform
builds; threading does not help a compute-bound kernel). Stage 24 Track 2 set out
to cut that cost.

The natural lever was the SIMD batch sincos that already exists in `fast_math`
(`mat3FromTrsArray` → 8-wide AVX2 `sincos`). Wiring it in surfaced three findings
that reshaped the track:

1. **Three divergent sincos in `fast_math`.** The scalar `sincos` (minimax
   polynomial), the AVX2 batch body (same polynomial, FMA + round-to-even), and
   the TRS scalar/tail (`libm std::cos/std::sin`) all produced different bits, so
   `mat3FromTrsArray` was not even internally bit-consistent. This is fatal for
   Render2D's byte-identity contract (a chunked/threaded caller must merge
   bit-for-bit). Fixed cross-repo in `fast_math` (`Melosyne-Math` `83b1977`): one
   shared register-level kernel, two-step Cody-Waite reduction, FMA-conditional,
   scalar == SIMD bit-exact on FMA targets, ~600x tighter accuracy. See that
   repo's `docs/TRIG_SINCOS_KERNEL.md`.

2. **The real speedup lever is hardware FMA, not explicit batching.** Render2D
   builds at the baseline ISA (no `-mavx2`/`-mfma`; "Stage 21C SIMD declined").
   Measured on 1M rotating world-transform builds:

   | build | `TransformSystem` (scalar) | `BatchedTransformSystem` |
   |---|---|---|
   | baseline (no FMA) | ~28–50 ms | ~24–40 ms |
   | AVX2+FMA | **~5.7–7 ms** | ~6.6–7.8 ms |

   The ~5–7x jump (baseline-dependent; re-measured 2026-06-16 at 27.6→5.7 ms ≈
   4.9x on a warm run, up to ~7x from a ~50 ms cold baseline — the FMA absolute is
   the stable ~5.7–6.9 ms end) comes from `std::fma` becoming a single hardware
   instruction once `-mfma` is set — it applies to the **existing** `TransformSystem`. The explicit
   8-wide `BatchedTransformSystem` is **bandwidth-bound** (AoS→SoA gather + Mat3
   scatter) and is *not* a clear win: ~0.9x on FMA, ~1.26x on non-FMA.

3. **Unconditional `std::fma` regresses non-FMA builds.** An early version forced
   `std::fma` in the scalar kernel; without FMA hardware that lowers to a libm
   call, regressing the baseline build 41 ms → 113 ms. The `fast_math` fix made
   the scalar path FMA-conditional, so the baseline build is back to ~50 ms.

## Decision

1. **Adopt the unified `fast_math` sincos kernel** (already landed + pushed,
   `83b1977`). Render2D consumes it through `Core/Types.hpp`.

2. **Add `RENDER2D_ENABLE_AVX2` (default `OFF`).** When `ON` it applies
   `-mavx2 -mfma` (`/arch:AVX2` on MSVC) to the `Render2D` INTERFACE target, so
   every consumer compiles with hardware FMA. The `clang-ninja-perf` preset sets
   it `ON`; `clang-ninja-debug` stays at the baseline ISA (so the non-FMA path
   keeps being exercised). This raises the minimum CPU for AVX2 builds to
   **x86-64-v3 (Haswell, 2013+)** — a deliberate, opt-in build-contract change.

3. **Add `BatchedTransformSystem<Provider, Dim>`** (`include/Render2D/System/`):
   tiles the input, gathers AoS `Transform` → SoA, calls `MMath::mat3FromTrsArray`,
   scatters to `WorldTransform`. It is byte-identical to a single-threaded
   per-element build using the same general formula (`MMath::sincos`, no
   `rotation==0` fast path), so it is deterministic and chunk-invariant. It is
   **kept** (per the measurement it is marginally faster on non-FMA builds and
   serves as the integration/contract test for `mat3FromTrsArray`), but it is
   **not** promoted as the default transform path: `TransformSystem` remains the
   reference, and `SpatialCullSystem` (Track 1) remains preferred for static
   workloads (its `rotation==0` fast path skips trig entirely).

### Byte-identity note

`BatchedTransformSystem` differs from `TransformSystem::run` only for
`rotation == 0`: `TransformSystem`'s fast path writes `m01 = +0.0`, while the
general formula yields `m01 = -(sy*sin 0) = -0.0`. The values are numerically
equal (`-0.0 == +0.0`) and downstream-irrelevant; only the stored sign bit
differs. The determinism test compares against the general-formula reference
(byte-identical) and against `TransformSystem` under float `==` (which treats
`±0` equal).

## Consequences

- AVX2 builds (the perf preset, and host engines that opt in) get ~5–7x on the
  rotating world-transform build for free on the existing `TransformSystem`, plus
  the ~600x-tighter sincos accuracy. Baseline builds are unchanged in behaviour
  and back to their prior performance (regression fixed).
- The math kernels are correct and internally consistent on both ISA tiers; only
  the FMA tier guarantees scalar==SIMD bit-identity (and only it compiles the SIMD
  lanes), which is the tier where a SIMD/threaded batch path would be used.
- Debug 57/57, Perf 78/78 green with `RENDER2D_ENABLE_AVX2=ON`. The bench
  `render2d_batched_transform_bench` is compiled with `-mavx2 -mfma` regardless of
  preset so it always measures the SIMD path.

## Alternatives rejected

- **Explicit batching as the primary lever.** Disproven by measurement: the batch
  is bandwidth-bound and does not beat FMA-accelerated scalar `TransformSystem`.
- **Unconditional `std::fma` in the kernel.** Regresses non-FMA builds (libm
  call); replaced with the FMA-conditional `detail::fmaOrMulAdd`.
- **Relaxing Render2D's byte-identity contract to a ULP tolerance.** Unnecessary
  once the `fast_math` kernel was made scalar==SIMD bit-exact on FMA targets.
- **Enabling AVX2 globally (incl. debug).** Kept debug at baseline so the non-FMA
  path stays continuously tested.
