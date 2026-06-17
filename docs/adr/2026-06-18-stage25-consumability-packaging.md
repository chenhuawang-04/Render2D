# ADR: Consumability and packaging — install/export and `find_package(Render2D)` (Stage 25)

Status: Accepted

Date: 2026-06-18

## Context

Render2D is a header-only `INTERFACE` library (`Render2D::Render2D`) designed to be
merged into a host engine. The reinforcement roadmap (Stages 17–24) is closed, and
Stage 23 proved Render2D is mergeable by **source reuse**: a host that pulls the
Render2D source can reuse the systems through `std::span` (23A) and drive host data
to a real presented frame (23D).

Two distinct consumption models exist for a host project:

1. **Source reuse** — the host adds Render2D to its build (`add_subdirectory` /
   already-defined targets) and reuses `Render2D::Render2D` plus the engine-dep
   targets directly. This is the primary, documented merge path.
2. **Installed package** — the host calls `find_package(Render2D)` against an
   installed/exported package, with no Render2D source in its tree.

The dependency-resolution half of consumability already landed in **Stage 17E**
(ADR `2026-06-14-stage17e-engine-dep-fetch.md`): `CMakeLists.txt` resolves the four
engine dependencies in three tiers — reuse an existing target, else
`add_subdirectory` a local tree, else `FetchContent` from git — with per-dependency
git overrides. (The older "hardcoded absolute paths, fails fast if missing"
description in `CLAUDE.md` is stale and is corrected in Stage 25D.)

What was missing, confirmed by reading the build:

- No `install()` / `install(EXPORT)` / `Render2DConfig.cmake` anywhere in Render2D's
  own build — model (2) does not work at all.
- The McVector include is `$<BUILD_INTERFACE>`-only (`CMakeLists.txt:208`), with no
  `$<INSTALL_INTERFACE>` counterpart — an installed target could not find McVector
  headers.
- The engine dependencies' own installs are force-disabled (`MCENTER_INSTALL OFF`,
  `THREAD_CENTER_ENABLE_INSTALL OFF`, `SDL_INSTALL OFF`), so an exported
  `Render2D::Render2D` references targets that will not exist in a consumer's tree
  unless that consumer also resolves the dependencies.

Neither model was *verified* end-to-end by a downstream consumer build.

## Decision

Stage 25 = **consumability / packaging**, delivered as build wiring + a downstream
consumer test for both models + verification, with no change to any runtime,
component, or system contract.

1. **Both models are first-class and proven by a real consumer build**, not asserted.
   25A adds an external mini-project (`tests/packaging/consumer/`) that consumes
   Render2D by source reuse and is exercised by a `render2d.packaging_consumer`
   CTest (nested configure → build → run). 25B adds `install(TARGETS … EXPORT
   Render2DTargets)`, `install(DIRECTORY include/Render2D)`, and a
   `CMakePackageConfigHelpers`-generated `Render2DConfig.cmake` (+ version file,
   `Render2D::` namespace), verified by a second consumer that calls
   `find_package(Render2D)` against a temporary install prefix.

2. **The installed config re-resolves the engine dependencies** (reuse → local →
   fetch), the same three-tier logic the top-level build uses, before defining the
   imported `Render2D::Render2D` target. The resolver helper ships inside the
   package. Render2D does **not** vendor the private dependency headers and does
   **not** assume the dependencies are installed. This keeps the installed package
   consistent with the source-reuse model and with the host-merge contract (the host
   owns / supplies these dependencies).

3. **The fetch tier is verified, not just authored** (25C): a clean-cache configure
   that actually clones and builds the four dependencies from git. Two of the repos
   are private, so this requires git credentials (`ENGINE_DEPS_TOKEN`); when
   credentials are unavailable the check is skipped with a logged reason rather than
   silently passing — the same honesty Stage 17E's ADR established for the
   authored-but-unrun fetch tier.

4. **No contract changes.** Packaging only adds *how Render2D is consumed/installed*.
   The umbrella `Render2D.hpp` stays SDL/RenderDoc/font/thread-free; the
   `McVector`/no-`std::vector`, no-direct-Vulkan-memory-API, Strict POD, and
   zero-NOLINT red lines hold. The support targets
   (`render2d_thread_runtime_support`, and when enabled the font / present-host
   support targets) are exported alongside the core so a host can opt into them, but
   the present-host stays OFF-able at merge.

## Consequences

- A host can consume Render2D either by source reuse (primary) or as an installed
  `find_package` package (secondary), and both paths are guarded by a downstream
  consumer build in CTest — the public target surface can no longer regress silently.
- The McVector install-interface gap is closed, so the installed include interface is
  complete.
- The installed `Render2DConfig.cmake` carries the three-tier dependency resolver, so
  installing Render2D does not require pre-installing its (partly private) engine
  dependencies — they are resolved at the consumer's configure time exactly as in a
  source build.
- The fetch tier gains a real (credential-gated) verification, closing the
  "authored but unrun" gap Stage 17E recorded.
- Stage 25D refreshes the stale dependency narrative in `CLAUDE.md` /
  `docs/PROJECT_INDEX.md` so the docs match the three-tier resolver that has existed
  since Stage 17E.
- This reopens the reinforcement roadmap past Stage 24 as an optional engineering
  stage; it adds no rendering features and changes no runtime behavior.

## Verification

- 25A `render2d.packaging_consumer`: a standalone project consumes
  `Render2D::Render2D` by source reuse (configure + build + run). 25B
  `render2d.packaging_installed`: install the package to a temp prefix, then
  `find_package(Render2D)` from a consumer (the config re-resolves the engine
  deps and re-attaches them). Both run in the default suite.
- Component install verified: `cmake --install --component Render2D` yields a
  clean tree (Render2D's headers + the four CMake package files), no dependency
  pollution, no vendored private headers; `install(EXPORT)` configures cleanly
  because the engine-dep / Vulkan links are `$<BUILD_INTERFACE:>`-only and the
  config re-attaches them.
- 25C `scripts/verify_fetch_tier.sh`: the offline-mechanism mode (tier-3
  FetchContent clone of fast_math + McVector via local git remotes) is verified
  green; the `--online` full mode (all four from github, the private repos via
  `ENGINE_DEPS_TOKEN`) is what CI's hosted full-build runs (Stage 17E). The
  script fails loudly when the network is unavailable rather than false-passing.
- Gate at closeout: Debug 66/66, Perf 87/87, `RENDER2D_BUILD_PRESENT_HOST=OFF`
  whole-tree 60/60 with 0 SDL/present-host/RenderDoc references, constraint scan
  3/3, clang-tidy clean, `git diff --check` clean.
