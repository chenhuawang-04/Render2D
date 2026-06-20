# ADR: Stage 26 — reproducibility, CI gate, and v0.1.0 release closeout

Status: Accepted

Date: 2026-06-20

## Context

The reinforcement roadmap (Stages 17–25) left Render2D a complete, host-mergeable
2D rendering backend. An external review of the public repository
(`deep-research-report`) found no high-confidence correctness bug, but flagged a
cluster of **engineering-quality** gaps that keep the repo from being something an
outside developer can reproduce, trust, and consume:

1. **Dependency reproducibility.** The fetch-tier `*_GIT_TAG` defaults tracked the
   moving `master` branch, so a clean fetch could resolve different dependency
   commits at different times. Two of the four engine dependencies
   (`MelosyneMemoryCenter`, `Vector`) were private, so a creditless clean/CI build
   was impossible.
2. **CI cache key too narrow.** The `engine-deps` cache key hashed only
   `CMakeLists.txt`, even though the real dependency URLs/refs live in
   `cmake/Render2DDependencies.cmake` — editing the resolver or a pin would not
   invalidate the cache.
3. **Weak push/PR gate.** The real configure/build/ctest/clang-tidy/bench job
   (`full-build`) ran only on manual `workflow_dispatch`; ordinary pushes saw only
   the lightweight `portable-checks`.
4. **Documentation drift.** `docs/ARCHITECTURE.md` still listed the Stage 19
   text/font path and the Stage 21 parallel tail under "not implemented yet", while
   `docs/PROJECT_INDEX.md` (and the code) showed them implemented and tested;
   `ReinforcementPlan.md` carried per-stage "local-not-pushed/HOLD" notes that were
   no longer true.
5. **Version/delivery disconnect.** `project(VERSION 0.1.0)` existed, but there was
   no tag, release, or changelog to map it to a Git state.

These are dependency-resolution, supply-chain, and delivery contracts, so this
closeout warrants an ADR (the repo rule for provider/storage/API/dependency
changes). It changes **no** runtime, component, or system contract, and preserves
every red line.

## Decision

**Make the dependency supply chain public, pinned, and reproducible; turn the real
build into an auto-gate; reconcile the docs; and ship a tracked `v0.1.0`.**

1. **Public + pinned engine dependencies.** `MelosyneMemoryCenter` (MemoryCenterNew)
   and `Vector` (McVector) are made public (fast_math and ThreadCenter already were).
   The fetch-tier `*_GIT_TAG` defaults in `cmake/Render2DDependencies.cmake` are
   pinned to exact commits instead of `master`:
   - MemoryCenterNew `f3f9881aa6a2119a974b6f9d4be7c70ed6358aff`
   - fast_math `83b1977f0c6549512c308ea8295e7ff908bc1849`
   - Vector_New (McVector) `21afc616f0b53df97085bf7fcf9b2d42f8e4c159`
   - ThreadCenter `c2944cc87534e12b9790d09acf7f8c1f1e93ae4d`

   Each remains an overridable `CACHE STRING`, so a host merge (tier-1 target reuse)
   or a local checkout (tier-2) is unaffected; only the tier-3 git fetch changes,
   and it now needs no credentials.

2. **CI hardening.** The `engine-deps` cache key hashes
   `cmake/Render2DDependencies.cmake` and `.gitmodules` in addition to
   `CMakeLists.txt`; the credential step is now optional (deps are public); and
   `full-build` runs on every push to `main`/`master` plus on demand
   (`github.event_name == 'push' || 'workflow_dispatch'`), skipped on PRs so
   `portable-checks` bounds fork cost.

3. **Documentation reconciliation.** `docs/ARCHITECTURE.md` gains a Stages 17–26
   implemented inventory and scopes its "not implemented" list to genuine
   host/production responsibilities; `README.md` reflects the public, creditless,
   pinned fetch and the auto-gated full build; `ReinforcementPlan.md` gets a
   current-status banner that supersedes its point-in-time "local-not-pushed" notes.

4. **Release.** Add `CHANGELOG.md`, tag `v0.1.0`, and publish a GitHub release —
   the first delivery entity `project(VERSION 0.1.0)` maps to.

5. **Host-minimal preset.** Add `clang-ninja-host-min`
   (`RENDER2D_BUILD_PRESENT_HOST=OFF`) so the merge-time build (the host owns the
   window/surface; `docs/MERGE_GUIDE.md`) has a one-line entry point, countering the
   "default favours in-repo validation" footgun without changing the default.

## Consequences

- A clean machine (or hosted CI) can now configure/build Render2D with **no
  credentials and a reproducible dependency set**. `scripts/verify_fetch_tier.sh`
  and the auto-gated `full-build` exercise the public fetch path end-to-end.
- Pins must be **bumped deliberately**: to take a newer dependency commit, update the
  matching `*_GIT_TAG` default (the CI cache key now invalidates on that edit).
- `full-build` cost rises (it runs on every integration push), accepted in exchange
  for a real gate; PRs still see only `portable-checks`.
- No core/runtime/component/system contract changes; the windowless, span-only,
  McVector-only, MemoryCenter-allocator, provider/dim, and present-host-isolation red
  lines are all intact. The test-only ECS and present-host scaffolding remain
  merge-time removal targets.

### Deliberately deferred (documented, not done here)

- **Automated real-GPU CI** (self-hosted/GPU runner). Hosted CI has no GPU, so
  `render2d.vulkan_*`/present tests still skip; GPU paths are verified locally.
- **Coverage reporting** and **branch-protection / required-checks** — left as
  governance follow-ups for the maintainer to enable, since required checks interact
  with the solo-maintainer direct-to-`master` workflow.

## Verification

- Engine deps confirmed public; the four pinned commits resolve. The local Debug
  configure re-runs `cmake/Render2DDependencies.cmake` cleanly with the pins in place
  (local tier used in-repo; the pins drive the tier-3 fetch exercised by CI).
- `scripts/scan_constraints.sh` 3/3; `python3 -c json.load` validates
  `CMakePresets.json` (host-min preset added); `git diff --check` clean.
- The full Debug/Perf suites are unchanged by this docs/CMake-defaults/CI closeout
  (no compiled code changed); the hosted `full-build` run triggered by the closeout
  push validates the public, pinned, creditless fetch on a clean machine.
- `v0.1.0` tag + GitHub release published; `CHANGELOG.md` maps it to this Git state.

## Update — Stage 26 follow-ups: real-GPU verification + coverage (2026-06-20)

Two of the three deliberately-deferred items above are now addressed; the third is
recorded as a deliberate choice. No core/runtime/component/system contract changes
(test scaffolding + a build option + scripts + CI workflows only); every red line
intact.

1. **Real-GPU verification — without a false-green.** Every `render2d.vulkan_*` test
   funnels through `createVulkanSmokeContext()` and every present test through
   `WindowTestHarness`, both of which return 0 (skip) when no device/display exists —
   so a green `ctest` cannot, by itself, prove a GPU path ran. Rather than edit all
   ~27 tests, we add **two capability-gate canaries**:
   - `render2d.gpu_presence_gate` (always built) and
   - `render2d.present_capability_gate` (present-host-gated).

   They skip by default (a no-op on headless CI), but when `RENDER2D_REQUIRE_GPU` /
   `RENDER2D_REQUIRE_PRESENT` is set they **hard-fail** if the device/present path is
   absent. The key insight: *a present device makes every other GPU/present test run
   its real path*, so a single canary asserting "a device exists" upgrades the whole
   suite's green into a GPU-ran guarantee — zero changes to the existing tests. The
   flag reader is `tests/support/EnvRequire.hpp`. Feature-specific paths
   (bindless/descriptor-indexing, validation layer) stay capability-gated, not
   device-gated — the canary reports them but does not require them.

   Delivery: `scripts/run_gpu_verification.{sh,ps1}` arm the gates and run Debug+Perf
   (usable on any GPU box now, no runner needed); `.github/workflows/gpu.yml` is a
   `workflow_dispatch`-only, `runs-on: [self-hosted, gpu]` job that is **inert** until
   a `gpu`-labelled runner is registered (so it never blocks the hosted CI), with the
   setup runbook in `docs/CI_SELF_HOSTED_GPU.md`. This is the cross-platform shape of
   the deferred "automated real-GPU CI" item: the repo side + local verification are
   done; standing up the runner is a one-time maintainer/hardware step.

2. **Coverage reporting.** `RENDER2D_ENABLE_COVERAGE` (Clang-only, OFF by default)
   adds `-fprofile-instr-generate -fcoverage-mapping` to the Render2D-owned test
   targets via `render2d_apply_strict_warnings` (the chokepoint that already skips
   `third_party/`), so instrumentation stays scoped to our code + the headers the
   tests include. `scripts/run_coverage.sh` runs the suite, merges the profiles, and
   emits a text + HTML report **scoped to `include/Render2D/`** with no external
   service or token (private repo → no Codecov). `.github/workflows/coverage.yml` is
   `workflow_dispatch`-only and uploads the report as an artifact + job summary. On a
   header-only library instrumented across ~70 binaries, llvm-cov prints a benign
   "functions have mismatched data" warning (differing per-TU instantiations cannot be
   merged); the aggregate is still representative.

3. **Branch protection / required checks — intentionally NOT configured.** The
   maintainer keeps the solo, direct-to-`master` workflow; required checks interact
   poorly with it, and the GitHub feature would need a paid plan on this private repo.
   Recorded here as a deliberate choice, to be revisited if the contributor model
   changes.

### Verification (follow-ups)

- Both canaries build/link clean and run green under `ctest`. The armed→fail /
  unset→skip / `=0`→skip decision was exercised against the real `EnvRequire.hpp`
  (no-device stub → exit 1 when armed, exit 0 otherwise); the satisfied/skip paths
  ran on a real device (Intel Iris Xe, Vulkan 1.4) and a real present path
  (640×480 B8G8R8A8 swapchain, 2 images).
- `scripts/run_gpu_verification.sh` (Debug) drove **72/72** tests green with both
  gates armed on a GPU+display box.
- `scripts/run_coverage.sh` produced a report end-to-end (Windows/Git-bash):
  `include/Render2D/` TOTAL ≈ 85% region / 96% function / 83% line coverage.
- `scripts/scan_constraints.sh` clean; the new test TUs are clang-tidy clean;
  `git diff --check` clean.
