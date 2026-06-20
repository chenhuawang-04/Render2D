# Changelog

All notable changes to Render2D are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project aims to
follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Render2D is a header-only `INTERFACE` library validated through CTest executables
(there is no application or shared/static-library output). A "release" here is a
tagged, reproducible snapshot of the headers, build/packaging surface, and docs —
not a binary artifact. A consumer pins a tag and builds against it by source reuse
(`add_subdirectory`) or `find_package(Render2D)`.

## [Unreleased]

### Added
- **Real-GPU verification (closes the "green ≠ GPU ran" gap).** Two capability-gate
  canaries — `render2d.gpu_presence_gate` and `render2d.present_capability_gate` —
  graceful-skip by default (so hosted CI stays green) but **hard-fail** under
  `RENDER2D_REQUIRE_GPU` / `RENDER2D_REQUIRE_PRESENT` when no device/display is
  present. Because a present device makes every other `vulkan_*`/present test run its
  real path, a green run with the flags armed proves the GPU paths executed this pass.
  Driven by `scripts/run_gpu_verification.{sh,ps1}` (run it on any GPU box today) and
  the `workflow_dispatch`-only, self-hosted `.github/workflows/gpu.yml` — inert until a
  `gpu`-labelled runner is registered (`docs/CI_SELF_HOSTED_GPU.md`).
- **Coverage reporting.** `RENDER2D_ENABLE_COVERAGE` instruments the Render2D-owned
  test targets with Clang source-based coverage; `scripts/run_coverage.sh` produces a
  text + HTML report scoped to `include/Render2D/` with no external service or token,
  and the `workflow_dispatch`-only `.github/workflows/coverage.yml` uploads it as an
  artifact + job summary.

### Notes
- **Branch protection / required checks: intentionally not configured.** The
  solo-maintainer direct-to-`master` flow is kept as-is (and the GitHub feature would
  need a paid plan on a private repo); revisit if the contributor model changes.

## [0.1.0] - 2026-06-20

First tagged release. Render2D is a C++23, component-first, Vulkan-native 2D
**rendering backend** — windowless and span-only by design, proven in-repo to be
embeddable into a host engine with no changes to its contracts. This tag captures
the renderer kernel (Stages 0–25) plus a reproducibility/release-readiness closeout
(Stage 26).

### Renderer (Stages 0–25)

- **CPU pipeline.** Strict-POD ECS components, stateless span-only systems, and the
  sprite data flow `Transform → Bounds → Culling → CommandBuild → SpriteInstance →
  Batch` (with an optional `DrawSort`), plus the parallel text path. A fused
  `SpatialCullSystem` front-end (Stage 24).
- **Vulkan runtimes.** `id + generation` resource references with stale-slot
  rejection; swapchain/present, sync, command (incl. per-thread), submit, resource,
  pipeline, descriptor, sampler, upload-ring, and texture-atlas runtimes. All buffer
  /image memory goes through `VulkanMemoryCenterAllocator`.
- **Sprites & textures.** Real offscreen sprite draw, texture sampling, multi-texture
  /multi-packet batches, texture-atlas UV regions, and Stage 20 bindless
  descriptor-indexing.
- **Text/font (Stage 19).** Real UTF-8 decoding, SheenBidi itemization, HarfBuzz-over
  -FreeType shaping + glyph rasterization (`FontShapeRuntime`), glyph atlas residency
  /packing (`GlyphAtlasRuntime`), and an end-to-end Vulkan glyph-coverage text draw.
  FreeType/HarfBuzz/SheenBidi are isolated behind `render2d_font_runtime_support`.
- **Parallel tail (Stage 21).** ThreadCenter-backed `ThreadedTextCpuPipelineRuntime`,
  `ThreadedDrawSortRuntime`, and `ThreadedBatchRuntime`, each byte-identical to its
  single-thread reference.
- **Present-host (Stage 22).** An optional, isolated SDL3 window + `VkSurfaceKHR`
  provider behind `RENDER2D_BUILD_PRESENT_HOST` (default ON in-repo, OFF at merge):
  on-screen present loop, visible-capture == offscreen baseline, RenderDoc frame
  capture, and a reusable `tests/support/WindowTestHarness.hpp` with a scene→window
  demo and a present frame-timing benchmark. The core stays windowless.
- **Host-merge readiness (Stage 23).** A host-shaped `HostLikeEcs` adapter proven
  byte-identical to the test ECS, the end-to-end `host_present_frame` capstone (first
  real sprite draw to reach a swapchain), and `docs/MERGE_GUIDE.md`.
- **Consumability/packaging (Stage 25).** Three-tier engine-dependency resolution,
  install/export for `find_package(Render2D)`, and downstream-consumer smokes for
  both source reuse and the installed package.

### Reproducibility & release closeout (Stage 26)

#### Added
- `CHANGELOG.md` (this file) and the `v0.1.0` tag/release — the first delivery entity
  the existing `project(VERSION 0.1.0)` maps to.
- `clang-ninja-host-min` CMake preset — the host-integration-shaped build
  (`RENDER2D_BUILD_PRESENT_HOST=OFF`), so the merge-time configuration has a one-line
  entry point instead of only a default that favours in-repo validation.
- `.github` issue/PR templates that carry the repository's scope discipline.

#### Changed
- **Engine dependencies are now public and pinned.** `MelosyneMemoryCenter`
  (MemoryCenterNew) and `Vector` (McVector) were made public (fast_math and
  ThreadCenter already were), so a clean/CI fetch needs **no credentials**. The
  fetch-tier `*_GIT_TAG` defaults are pinned to exact commits for byte-reproducible
  fetches instead of tracking `master`.
- **CI hardening.** The `engine-deps` cache key now hashes
  `cmake/Render2DDependencies.cmake` and `.gitmodules` in addition to `CMakeLists.txt`
  (so changing the resolver or a pin invalidates the cache), and the `full-build` job
  runs automatically on every push to `main`/`master` (it previously only ran on
  manual dispatch) now that no secret is required.
- **Documentation drift reconciled.** `docs/ARCHITECTURE.md` no longer lists the
  Stage 19 text/font path and Stage 21 parallel tail as "not implemented"; it now
  carries a Stages 17–26 implemented inventory, and its "not implemented" list is
  scoped to genuine host/production responsibilities. `README.md` reflects the public,
  no-credential dependency fetch. `ReinforcementPlan.md` carries a current-status
  banner superseding its per-stage "local-not-pushed/HOLD" point-in-time notes.

### Known limitations / follow-ups (out of scope for this tag)

- **No automated real-GPU coverage.** Hosted CI has no GPU, so `render2d.vulkan_*` and
  present tests skip (green ≠ GPU paths exercised). GPU paths are verified locally; a
  self-hosted/GPU runner is a follow-up.
- **No coverage reporting** and **no branch-protection / required-checks** configured.
- The production ECS, asset pipeline, windowing/app loop, and the rest of the host
  engine remain intentionally out of scope (see `docs/ARCHITECTURE.md` → *Scope and
  non-goals*); the test-only ECS/present-host scaffolding are merge-time removal
  targets (`docs/MERGE_GUIDE.md`).

[Unreleased]: https://github.com/chenhuawang-04/Render2D/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/chenhuawang-04/Render2D/releases/tag/v0.1.0
