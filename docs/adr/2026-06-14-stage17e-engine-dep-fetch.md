# ADR: Stage 17E — Engine Dependency Fetch + Hosted Full-Build CI

Date: 2026-06-14

## Status

Accepted. **Supersedes** two decisions of
`2026-06-14-stage17-build-portability-ci.md`: decision #1 (dependency portability
via *local-override only*, explicitly **not** adding a fetch mechanism) and the
CI topology of decision #2 (the full build runs **self-hosted only**). The rest
of that ADR — the umbrella `RENDER2D_ENGINE_DEPS_ROOT`, the scripted constraint
scan, the standalone validation smoke, the `.sh`-LF guard, and the always-on
hosted `portable-checks` tier — stands unchanged.

No ECS/system/runtime contract changed: systems still take `std::span`,
components stay Strict POD, refs remain `id + generation`, and the umbrella
`Render2D.hpp` is untouched. Only the build's dependency-*acquisition* path and
the CI topology changed.

## Context

The original Stage 17 ADR rested on a premise that has since become false: that
the four engine dependencies "cannot be fetched on a clean machine — `Vector_New`
has no git remote and the others are private." Since that ADR closed:

- All four engine deps now have git remotes under `chenhuawang-04`:
  `MelosyneMemoryCenter` (private), `Vector` (private, McVector — newly
  published), `Melosyne-Math` (public, fast_math), `Melosyne_ThreadCenter`
  (public). All four are pushed and current.
- McVector itself was tidied and published with its own three-tier MemoryCenter
  resolver (reuse → local → fetch), proving the pattern.

So a fetch-based clean/CI build is now possible. Two repos remain private, which
makes credentials — not fetchability — the residual constraint.

## Decision

### 1. Render2D resolves each engine dependency in three tiers

`CMakeLists.txt` replaces the local-path-only handling (fail fast if a sibling
tree is missing) with a per-dependency resolver, downloading only as a last
resort:

1. **Reuse** — if the enclosing project already defines the dependency's target
   (`Center.Memory.Headers` / `fast_math` / `Center.Thread.Headers`), reuse it
   untouched. This also makes the host-engine merge (Stage 23) conflict-free.
2. **Local** — else `add_subdirectory` a local tree (umbrella
   `RENDER2D_ENGINE_DEPS_ROOT` or a per-dep `*_SOURCE_DIR` override) — no
   download. Unchanged from before.
3. **Fetch** — else `FetchContent` from git. New per-dep
   `RENDER2D_*_GIT_REPOSITORY` / `*_GIT_TAG` cache variables (defaulting to the
   `chenhuawang-04` repos `@ master`) let CI pin an exact commit. The fetch
   recurses submodules (mimalloc/json for MemoryCenter, taskflow for
   ThreadCenter).

The three subdirectory-style deps go through a `render2d_provide_engine_dependency`
macro (a macro, so `add_subdirectory`/FetchContent run in top-level scope exactly
as the prior inline calls did). `Vector_New` (McVector) stays **header-include**
consumption: a local include tree if present, else a headers-only fetch
(`SOURCE_SUBDIR` points at a path with no `CMakeLists.txt`, so FetchContent
populates without `add_subdirectory`), after which its `include/` is used. Each
tier asserts the expected target/header actually materialized, with an actionable
`FATAL_ERROR` otherwise.

### 2. The full-build CI tier moves to hosted runners

`full-build` changes from `runs-on: [self-hosted, render2d]` to
`runs-on: ubuntu-latest`. It installs clang 22 + Ninja (apt.llvm.org `llvm.sh`)
and the Vulkan loader/headers/validation layers (`libvulkan-dev`,
`vulkan-validationlayers`), configures a git credential rewrite from an
`ENGINE_DEPS_TOKEN` secret so the two private deps fetch over HTTPS, shares a
cached `FETCHCONTENT_BASE_DIR` across the Debug and Perf configures, then runs
the same configure → build → `ctest` (Debug + Perf) → clang-tidy → constraint
scan → bench-smoke sequence. It stays **manual** (`workflow_dispatch`) to control
cost; the `engine_deps_root` input now defaults to empty (fetch), and may be set
to a pre-staged local root to skip fetching.

## Consequences

- **A clean-machine build no longer needs pre-staged sibling checkouts** — only
  git credentials for the two private repos. The `portable-checks` tier is
  unchanged.
- **The local tier is behavior-preserving.** Verified: Debug `ctest` 52/52, Perf
  61/61 with the existing local root (all four resolve "no fetch").
- **The fetch tier is verified** at the mechanism level: an empty-root configure
  correctly enters the fetch tier with the right GitHub URLs for all three
  subdir deps, and FetchContent is proven end-to-end by cloning `fast_math` from
  a local git repo (target defined, configure clean). A live full GitHub fetch
  was not completed from the dev sandbox (flaky egress), but `gh`/`push` confirm
  GitHub reachability on a normal network.
- **Honest limitations (the accepted tradeoffs):**
  - The hosted `full-build` workflow is **authored but not yet executed**:
    Render2D still has no git remote, so CI cannot run until the repo is pushed
    **and** the `ENGINE_DEPS_TOKEN` secret is added. First green run will confirm
    the toolchain/Vulkan install and fetch steps.
  - **No GPU on hosted** → every `vulkan_*` smoke (incl. the validation smoke)
    skips; the build is green but GPU paths are not exercised there.
  - The clang-22 install (apt.llvm.org) and the Vulkan apt packages target the
    current `ubuntu-latest` (noble) and may need bumping as the image moves.
  - The two private repos remain a **credential** dependency; making them public
    would remove the `ENGINE_DEPS_TOKEN` requirement entirely.
- This reverses the original ADR's "do not add fetch" and "self-hosted only"
  precisely because their premise (unfetchable deps) no longer holds. Those
  choices were correct then; the facts changed.
