# ADR: Test-only lightweight ECS + asset framework

Status: Accepted

Date: 2026-06-19

## Context

Render2D is validated entirely through CTest executables — there is no app. The
existing scaffolding under `tests/support/` proves individual contracts
(`ComponentStreamStorage`, the Stage 23A `HostLikeEcs`), but two gaps made it hard
to demonstrate the renderer is *usable* the way a host engine would use it:

1. The existing "ECS" types are dumb data containers — no entity lifecycle
   (create/destroy, generational handles, per-entity component add/remove,
   queries). A reviewer could not author a scene the way an engine does.
2. There was no asset path at all. Tests hardcode `texture_id` / `material_id`
   integers; nothing loaded a real image file or turned it into a sprite.

The request was for "the most lightweight ECS and asset framework, as part of the
test framework, to validate the project is usable — without affecting the core
rendering code."

This sits against a hard scope rule (`CLAUDE.md`, `docs/ARCHITECTURE.md` → *Scope
and non-goals*): the production ECS/scene graph and the asset pipeline are
**out of scope for Render2D's core** and are the host engine's job at merge. The
reconciliation is the same one already used for `HostLikeEcs` and the Stage 22
present-host: this is **test-only scaffolding under `tests/`**, never compiled into
the core, the umbrella, or `src/`. It validates the public, span-only system
boundary from a realistic angle; it is not a step toward an in-core ECS or asset
pipeline.

"Real image-file loading" was an explicit choice, which means a real decoder. stb
(`stb_image` / `stb_image_write`) is the standard single-header option and matches
the existing vendored-single-header precedent (`third_party/renderdoc`).

## Decision

Add a lightweight ECS + asset framework strictly as test scaffolding:

1. **`tests/support/MiniEcs.hpp`** — a small, type-safe, header-only ECS:
   generational `MiniEntity` handles with slot recycling; per-component-type packed
   sparse-sets; `add`/`get`/`has`/`remove<Component>`; and `gatherRenderInputs()`
   which packs the renderable input set (`Transform` / `LocalBounds` /
   `VisibilityMask` / `Sprite`) into **row-aligned dense columns** so the unchanged
   span-only systems consume it through `std::span` alone. McVector-only; the
   Strict POD components are unchanged.

2. **`tests/support/ImageFile.{hpp,cpp}` + `third_party/stb`** — a clean, stb-free,
   McVector-based image API (`loadImageRgba8` / `writeImageRgba8Png`).
   `ImageFile.cpp` is the **one** TU that compiles the vendored stb headers, behind
   a dedicated **`render2d_test_image_io`** static library that includes stb as a
   **SYSTEM** include (so stb's warning-dirty internals never trip
   `-Wall -Wextra -Wpedantic -Werror`) and is deliberately not given the strict
   warning flags. stb is never included from a header, the umbrella, or the core.

3. **`tests/support/AssetRegistry.hpp`** — a CPU-side asset framework: load textures
   from disk into stable `TextureHandle`s, register logical materials, look them up
   by name, and author `Sprite` components that reference them. No Vulkan; the GPU
   test feeds the decoded pixels to the runtimes itself.

4. **Two validation tests.** `render2d.mini_ecs_pipeline` (always runs): author a
   scene into `MiniEcs`, gather it, run the full CPU chain, and assert it is
   `memcmp`-identical to the same scene built directly into plain arrays, plus an
   entity-lifecycle section. `render2d.asset_scene_render`: load real PNGs through
   the registry, author a `MiniEcs` scene referencing them, run the CPU chain, and —
   on a GPU — upload the loaded texture, sample it across a sprite via the unchanged
   sprite pipeline + render encoder, and assert the readback is the texture's color;
   the GPU half skips gracefully without a device.

No runtime, component, or system contract changes. The systems are driven entirely
through `std::span` (the same boundary `HostLikeEcs` proves), so a brand-new storage
type compiling against them *is* the guarantee that the boundary held.

## Consequences

- Render2D now has an end-to-end usability proof: author entities → attach
  components → load a real image asset → run the CPU chain → (GPU) draw a sampled
  sprite → read it back, all from test scaffolding, with the core untouched.
- A new vendored dependency (stb) enters the repo, but only as a test-only,
  SYSTEM-included, single-TU library. The constraint scanner does not scan
  `third_party/`; the `std::vector`/Vulkan-memory/local-math invariants still hold in
  all Render2D-owned source.
- The present-host-OFF whole-tree build is unaffected (no SDL/RenderDoc/present-host
  references); the two new tests are ungated and build/run in every configuration
  (the asset test's GPU half is a runtime skip, like every other `vulkan_*` test).
- This is explicitly **not** an in-core ECS or asset pipeline. `MiniEcs` and
  `AssetRegistry` are merge-time removal targets exactly like the existing test ECS
  (`docs/MERGE_GUIDE.md` §5): the host engine supplies the production versions.

## Verification

- `render2d.mini_ecs_pipeline`: 129-entity scene, gather → CPU chain is
  `memcmp`-identical to the plain-array path (visible/batch counts non-trivial); the
  lifecycle section exercises generational stale-handle rejection and slot reuse.
- `render2d.asset_scene_render`: writes two solid PNGs (≈78 bytes each), decodes
  them via stb, authors a 12-entity scene (→ 7 visible, 6 batches) whose draw
  commands carry the loaded textures' ids; on a present GPU it uploads the green
  texture and the framebuffer readback is byte-for-byte green.
- Gate at landing: Debug + Perf full suites green (the two new tests included), the
  `RENDER2D_BUILD_PRESENT_HOST=OFF` whole-tree build green with the new tests present
  and zero SDL/RenderDoc references, `scripts/scan_constraints.sh` 3/3, clang-tidy
  clean on the new TUs, `git diff --check` clean.
