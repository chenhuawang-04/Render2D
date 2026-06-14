# ADR: Stage 17 Build Portability & Regression Gate

Date: 2026-06-14

## Status

Accepted. Stage 17 (17A–17D) is closed. The dependency-acquisition contract, the
CI topology, and the scripted constraint gate recorded here are in force. No
ECS/system/runtime contract changed — this stage is purely build and gating
hardening.

## Context

The repository hardcoded `E:/Project/MelosyneTest/...` for its four engine
dependencies and had no CI of any kind. Two facts constrain what "portable build
+ CI" can mean here, and both were confirmed before this stage:

- **The four engine dependencies cannot be fetched on a clean machine.**
  `Vector_New` has no git remote at all; `MemoryCenterNew`, `fast_math`, and
  `ThreadCenter` live in private repositories. So neither `FetchContent` nor git
  submodules can uniformly assemble them on a hosted runner. (The font libraries
  — FreeType/HarfBuzz/SheenBidi — are already public submodules; that pattern does
  not extend to the engine deps.)
- **Render2D itself has no git remote yet.** A hosted CI cannot run until the repo
  is pushed somewhere.

Given those, the user made two scoping decisions that shape this ADR: the CI form
is **GitHub Actions only**, and dependency portability is delivered by
**improving the local-override path plus guidance** — not by adding a fetch
mechanism for the engine deps.

## Decision

### 1. Dependency portability via a first-class local-override path (17A)

A single umbrella cache variable `RENDER2D_ENGINE_DEPS_ROOT` (default
`E:/Project/MelosyneTest`) derives the four per-dependency paths
(`RENDER2D_MEMORY_CENTER_SOURCE_DIR`, `RENDER2D_FAST_MATH_SOURCE_DIR`,
`RENDER2D_VECTOR_NEW_INCLUDE_DIR`, `RENDER2D_THREAD_CENTER_SOURCE_DIR`); each
remains individually overridable and takes precedence. A
`render2d_require_engine_dependency()` helper replaces the four terse
`FATAL_ERROR` checks with actionable guidance — the expected marker path, the
variable used, the fix (umbrella vs individual var), and a README pointer. A new
root `README.md` documents the expected layout, the Vulkan SDK, the font
submodules, the presets, and the no-GPU skip. We deliberately did **not** add
`FetchContent`/submodules for the engine deps (they are not fetchable); the
override path is the portability mechanism.

### 2. GitHub Actions only, in two tiers (17B)

CI is a single workflow, `.github/workflows/ci.yml`, split by what each tier can
actually do given the dependency facts above:

- **`portable-checks`** runs on hosted `ubuntu-latest` for every push to
  `main`/`master` and every pull request. It needs no engine deps, Vulkan SDK,
  GPU, or build: the constraint scan, a merge-conflict-marker scan, and a
  `CMakePresets.json` validity check. This is the always-on safety net.
- **`full-build`** runs configure → build → `ctest` (Debug + Perf) → clang-tidy →
  constraint scan → bench smoke. Because the engine deps are not fetchable, it
  targets a **self-hosted runner** (label `render2d`) where those trees and the
  Clang/Ninja/Vulkan toolchain live, and is **manual** (`workflow_dispatch`, with
  the engine-deps root as an input) so it never queues against an offline runner.

The tiering is a forced consequence of the two scoping decisions, not a free
choice: hosted runners cannot build, so the build gate lives where the deps are.

### 3. Scripted constraint scan (17D)

`scripts/scan_constraints.sh` codifies the previously-manual source-invariant
scans (run by hand since the Stage 10K gate): no `std::vector` (use
`Render2D::McVector<T>`), no direct Vulkan memory API (route through
`VulkanMemoryCenterAllocator`), and no Render2D-local math structs (use the
`MMath` `Vec2`/`Mat3`/`Aabb2` aliases; the removed `Affine2X3` must not return).
It scans only Render2D-owned source (`include/`, `src/`, `tests/`, `bench/`) and
never the vendored `third_party/` trees, uses POSIX grep only (runs on Linux CI
and Windows Git-bash), and exits nonzero with `file:line` per violation. Both CI
tiers invoke it.

### 4. Standalone validation-layer smoke (17C)

`tests/vulkan_validation_layer_smoke_test.cpp` (`render2d.vulkan_validation_layer_smoke`)
builds its **own** instance with `VK_LAYER_KHRONOS_validation` +
`VK_EXT_debug_utils` and a debug messenger, then drives a real offscreen workload
(buffer upload → device copy → readback, plus an image create/destroy) through
the Render2D resource/command/sync/submit runtimes, asserting **zero** validation
errors (warnings are printed, not fatal). It reuses only the
physical-device/device selection helpers from `support/VulkanSmokeContext.hpp` —
never the shared no-layer instance — so the other ~50 vulkan smokes are
untouched. It skips (returns 0) when the validation layer is absent, no
ICD/instance is available, or no usable GPU is present.

### 5. Line-ending and hygiene guards

`.gitattributes` pins `*.sh` to LF so the scanner runs on Linux runners and under
Git-bash regardless of `core.autocrlf`. The date-prefixed local session
transcripts are added to `.gitignore`.

## Consequences

- **Clean-environment configure is proven.** A fresh configure with a bogus
  `RENDER2D_ENGINE_DEPS_ROOT` fails with the actionable message; a fresh configure
  with the default root succeeds via derivation.
- **The constraint scan is proven in both directions.** The clean tree passes
  (3/3 ok); a fixture injecting all three violations is caught and reported.
- **The validation smoke is proven in both directions.** On a GPU+SDK machine the
  workload runs validation-clean; an injected leaked buffer trips
  `VUID-vkDestroyDevice-device-05137`, confirming the messenger is live and the
  test can fail on a real error.
- **Honest limitation (the accepted tradeoff):** the `full-build` tier cannot run
  on stock hosted GitHub runners — it requires a self-hosted runner with the
  engine deps, because those deps are not fetchable. Until Render2D is pushed to a
  remote and such a runner is registered, only the `portable-checks` tier runs.
  Making the engine deps fetchable (a public/mirrored Vector_New remote, access to
  the private repos) would let the full tier move to hosted runners; that is a
  future decision, explicitly out of scope here per the local-override-only
  choice.
- No provider/storage/API/dependency *consumption* contract changed: systems still
  take `std::span`, components stay Strict POD, refs remain `id + generation`, and
  the umbrella `Render2D.hpp` is untouched. Only the build's dependency-*location*
  contract and the gating infrastructure changed.
- Stage 17 was intentionally deferred behind Stages 18–20 (functional delivery
  first) and is now closed as the engineering safety net beneath them. Remaining
  roadmap: Stage 21 (parallel tail), 22 (on-screen present), 23 (host-engine
  merge).
