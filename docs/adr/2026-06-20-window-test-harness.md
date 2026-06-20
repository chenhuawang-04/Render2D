# ADR: Window test harness (reusable on-screen present scaffolding)

Status: Accepted

Date: 2026-06-20

## Context

The Stage 22/23 on-screen tests each grew from the same template, and by Stage 23D
three of them (`present_visible_capture_smoke` 22D, `present_renderdoc_capture_smoke`
22E, `host_present_frame_smoke` 23D) carried near-identical copies of ~150 lines of
scaffolding:

1. window → instance → surface → present-capable device → queue bring-up, with a
   graceful skip at every gate (the headless-CI contract);
2. local copies of `ActiveSwapchain`, `recordSwapchainBarrier`, and
   `createCaptureSwapchain` (a swapchain whose images carry the TRANSFER usage the
   capture tests need);
3. the per-frame tail: acquire an image, copy a rendered offscreen image onto it,
   read both back, present, and compare.

The earlier sub-stages deliberately kept these local (22D's note: promoting them
would need templating + pulling `PresentHost` into the shared header). With three
copies in the tree and a request for a reusable **window test framework** to
present author-it-yourself scenes, the duplication is now worth consolidating.

Scope: the present-host (SDL3) is already an **optional, isolated, out-of-core**
subsystem (`docs/adr/2026-06-16-stage22-sdl3-present-host.md`); this harness is
test-only scaffolding on top of it, exactly like `PresentBringup`. It changes no
runtime, `PresentHost`, component, or system. The harness creates the
`VkInstance`/`VkDevice` for the *test* — the same thing each test already did
inline — so ProjectMergeTODO red line #11 (the host owns the window + instance +
device + surface at merge) is unchanged; `PresentHost` itself still never creates
an instance/device.

## Decision

1. **`tests/support/WindowTestHarness.hpp`** — a header-only harness in the
   `Render2DTest` namespace, linking the internal `render2d_present_host_support`
   target:
   - `kWindowSwapchainImageCapacity`, `ActiveSwapchain`, `recordSwapchainBarrier`,
     and `createCaptureSwapchain` promoted verbatim from the three tests.
   - `WindowTestHarness` — an RAII class that does the full window/instance/
     surface/device/queue bring-up in one `initialize(Config)` call (graceful skip,
     logged via a per-test prefix; a `require_dynamic_rendering` flag selects the
     1.3 vs 1.0 `PresentBringup` path) and tears everything down in the correct
     order in its destructor.
   - Frame helpers shared by the capture tests + demo: `surfaceSupportsUsage`,
     `acquireSwapchainImage`, `recordOffscreenToSwapchainCapture` (the readback
     tail), `submitPresentWaitFence`, and `readbackVariesAndIdentical`.

2. **Refactor the three capture tests onto it.** Each now expresses only what is
   unique to it — the gradient render (22D/22E) or the host sprite render (23D) —
   and drives the shared bring-up/swapchain/present helpers. 22E keeps its own
   no-readback record + RenderDoc-bracketed submit/present (it captures an artifact
   rather than reading back), using the promoted `recordSwapchainBarrier`. The
   refactor is behaviour-preserving: the exact Vulkan call sequences are unchanged.

3. **New `render2d.window_scene_present`.** Authors a scene with the test-only
   `MiniEcs` + `AssetRegistry` (loads a real PNG, registers a material, sprites
   reference the handles), runs the unchanged span-only chain to a `SpriteInstance[]`,
   and presents it to a hidden window via the harness, asserting the swapchain
   capture == the offscreen baseline (and that the frame varies). It uses the
   color-only sprite path — the proven Stage 23D present path; textured-sampled-on-
   GPU is already covered by `render2d.asset_scene_render`. The scene is authored
   directly in the sprite shader's NDC clip space (camera = NDC box), since clip
   projection is the host's vertex-shader concern (ProjectMergeTODO #32).

Run mode is **automated + headless-safe**: the window is hidden, the frame is
validated by readback, and every bring-up gate is a graceful skip — the same
discipline as the other present tests, so a full `ctest` run stays deterministic
and green on a headless box. There is no visible mode.

## Consequences

- The three capture tests shed their duplicated scaffolding; a new on-screen test
  is now roughly half the code (the demo is the proof). The single shared bring-up
  + capture path is also a single place to fix bugs.
- The harness creates a `VkInstance`/`VkDevice`, but that is test bring-up — the
  same thing the tests did inline. `PresentHost` is untouched and still never
  creates them; the host owns the window/instance/device/surface at merge.
- No runtime, `PresentHost`, component, or system contract changes; the
  swapchain/present runtimes are driven through their existing APIs.
- `RENDER2D_BUILD_PRESENT_HOST=OFF` whole-tree is unaffected: the harness and all
  four present-host tests live in the gated block and vanish in an OFF build. The
  demo additionally links `render2d_test_image_io` (stb) for the PNG load.
- `WindowTestHarness`, the demo, and the present-host tests are merge-time removal
  targets like the rest of the present-host scaffolding (`docs/MERGE_GUIDE.md` §5).

## Verification

- The three refactored tests run the real GPU path and report byte-identical
  results to before the refactor: 22D `640x480 visible capture == offscreen
  baseline (1228800 bytes)`, 22E `640x480 gradient frame presented (no RenderDoc)`,
  23D `640x480 host scene -> 4 sprite instances presented; swapchain == offscreen
  baseline`.
- `render2d.window_scene_present`: CPU `12 entities -> 4 visible, 1 batch (assets
  loaded from PNG)`, GPU `640x480 authored scene -> 4 sprite instances presented;
  swapchain == offscreen baseline (1228800 bytes)`.
- Gate at landing: Debug + Perf full suites green (the new test included), the
  `RENDER2D_BUILD_PRESENT_HOST=OFF` whole-tree build green with the demo + present
  tests absent and zero SDL/present-host/RenderDoc references,
  `scripts/scan_constraints.sh` 3/3, clang-tidy clean on the changed test TUs,
  `git diff --check` clean.

## Update — visible mode (2026-06-20)

The first cut hid the window unconditionally (it is validated by readback, so
nothing needs to be on screen), which is right for CI but meant a developer
running the demo saw *nothing*. Added an opt-in visible mode so the framework can
actually show what it renders:

1. **`PresentHost::pollShouldClose()`** — a small additive method on the
   present-host that drains the window's pending SDL events and returns whether the
   user asked to close it (window-close / quit / Escape). A visible present loop
   must pump events every frame or the OS marks the window "not responding"; this
   keeps all SDL usage inside `PresentHost` (the one file allowed to touch SDL),
   so tests still never call SDL directly. It creates no instance/device and only
   reads its own window's events — ProjectMergeTODO red line #11 is unchanged.
2. **`WindowTestHarness::Config::visible`** (default `false`) — when set,
   `initialize` opens a shown window instead of a hidden one. The default is bit-
   for-bit the previous behaviour (hidden), so every existing test and every ctest
   run is unaffected.
3. **`render2d.window_scene_present --visible`** — `main` parses `--visible`; with
   it, after the single readback-validated frame the demo enters a present loop
   (same fence/pool discipline as `present_loop_smoke`, Stage 22C) that re-renders
   and re-presents the authored scene each vsync, pumping events via
   `pollShouldClose()`, until the user closes the window (or a generous safety-cap
   frame count). The window is not resizable, so a non-Ok acquire (e.g. minimize →
   `SwapchainOutOfDate`) simply ends the loop. No readback in the loop — that was
   the single frame's job. Under ctest (no args) the window stays hidden and the
   loop is skipped, so the suite remains deterministic and headless-safe.

This is still test-only scaffolding over the optional present-host; it adds no
core, runtime, component, or system surface (the only library-side change is the
additive `pollShouldClose()` on the already-isolated `PresentHost`). Verified: the
default/CI path is byte-identical (Debug 69/69, Perf 90/90, the three capture tests
+ the hidden demo report the same `== offscreen baseline` results as before),
`RENDER2D_BUILD_PRESENT_HOST=OFF` whole-tree 62/62 with zero SDL/present-host
references (so `pollShouldClose` is compiled only when the present-host is on),
clang-tidy clean on the demo TU (which re-checks `PresentHost.hpp`), scan 3/3,
`git diff --check` clean. The `--visible` path was run manually: a real 640×480
window opens, presents the four-quadrant sprite scene, and closes on the X / Esc.

## Update — present frame-timing benchmark (2026-06-20)

The harness proved the present path *correct* (byte-identical capture); nothing
measured how *fast* it is. Added a present-path frame-timing benchmark on top of
the same harness:

1. **`render2d.window_scene_bench`** (`tests/window_scene_bench_test.cpp`) — authors
   a scalable grid of N sprites (all visible) via the test-only `MiniEcs`, runs the
   unchanged span-only chain (`SpatialCull → CommandBuild → SpriteInstanceBuild →
   Batch`) into a `HostFrameArena`, and presents the resulting instances in a timed
   loop, reporting per-frame latency avg / min / max / p50 / p99 and an approximate
   FPS. It constructs `Sprite` directly (no PNG/asset path), so unlike the demo it
   does not link `render2d_test_image_io`. CLI: `--sprites`, `--frames`, `--warmup`,
   `--present-mode fifo|immediate|mailbox`, `--visible`.
2. **Two additive harness helpers** — `selectPresentMode` (returns the preferred
   present mode if the surface supports it, else FIFO) and an optional `present_mode_`
   parameter on `createCaptureSwapchain` (defaults to FIFO, so the four existing
   callers are byte-identical). The benchmark requests IMMEDIATE/MAILBOX so it
   measures real per-frame cost instead of the vsync interval; the capture tests
   keep FIFO.

This is a *measurement tool, not a regression gate*: GPU timing is machine- and
load-dependent, so (unlike the deterministic CPU perf gate in `bench/`) it asserts
no wall-clock budget. The CPU chain is deterministic, so the ctest case still has
teeth — it asserts the chain produced exactly N visible instances + at least one
batch — while the GPU timing path runs a handful of frames purely to stay compiled
+ exercised, graceful-skipping on a headless box. Real numbers come from a manual
run with larger `--frames`/`--sprites`; `--visible` shows the grid and loops until
the window is closed (reporting over the first `--frames` measured frames).

Still test-only scaffolding over the optional present-host: no core/runtime/
component/system surface changes. Verified: Debug 70/70, Perf 91/91 (the new test
included), clang-tidy clean on the new TU, `scripts/scan_constraints.sh` 3/3,
`git diff --check` clean; `RENDER2D_BUILD_PRESENT_HOST=OFF` configures with the
benchmark target absent and zero SDL/present-host references in `build.ninja` (it
lives in the gated block with the other present tests). Observed locally: FIFO
vsync-locks to the display refresh, IMMEDIATE runs uncapped (e.g. ~0.4 ms/frame at
2000 sprites), confirming the present-mode selection reaches the swapchain.
