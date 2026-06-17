# ADR: SDL3 Present-Host Dependency (Stage 22A)

Date: 2026-06-16

## Status

Accepted. SDL3 is added as a fourth git submodule under `third_party/sdl`
(pinned to `release-3.4.10`) and built as a static library behind the new
`RENDER2D_BUILD_PRESENT_HOST` option, exposed only through the internal
`render2d_present_host_support` target. This is Stage 22A (build plumbing); the
real window/surface/swapchain/present path is Stage 22B onward.

## Context

Stage 22 adds on-screen presentation. The GPU-side runtimes already exist and
are headless contract-tested: `VulkanSwapchainRuntime::createSwapchain` /
`adoptSwapchain` (with `SwapchainOutOfDate` recreation) and
`VulkanPresentRuntime::acquireNextImage` / `present`. By design they take a
host-provided `VkSurfaceKHR` as input (`VulkanSwapchainCreateConfig::surface`) —
Render2D never creates a window or a surface itself. That is a hard scope rule:
windowing and the app loop are the host engine's job, not this rendering module's
(see `docs/ARCHITECTURE.md` → *Scope and non-goals*, and `ProjectMergeTODO.md`
red line #11 "the host owns the window + surface").

What was missing was a way to actually obtain a real surface to drive those
runtimes for visible verification. The user chose **SDL3** as the windowing
backend, introduced as a submodule, and explicitly asked that it **not** be a
throwaway test-only harness — they want a real, reusable on-screen rendering path
later. Adding SDL3 is a new third-party dependency and a new build/target
contract, which this repository requires to be recorded in an ADR (CLAUDE.md red
line: provider/storage/API/dependency-contract changes get an ADR).

The tension to resolve: "Render2D core stays windowless" vs. "a real, reusable
SDL3 windowing path lives in this repo."

## Decision

Reconcile the two by making SDL3 an **optional, isolated present-host
subsystem**, never part of the windowless core:

- **Submodule.** `third_party/sdl` → tag `release-3.4.10` (current stable SDL3;
  the 3.x series guarantees API/ABI stability, so the Vulkan-windowing API used
  later — `SDL_Vulkan_CreateSurface` / `SDL_Vulkan_GetInstanceExtensions` — is
  fixed across the line). `shallow = true` in `.gitmodules`, matching the
  freetype/harfbuzz/sheenbidi submodules. SDL's default branch is the 3.5.x
  development head, so a tag pin (not a branch) is mandatory.
- **Build option.** `RENDER2D_BUILD_PRESENT_HOST` (default **ON** in this repo
  for development/validation; a host engine sets it **OFF** at merge and supplies
  its own surface). When OFF, the SDL submodule is never `add_subdirectory`-ed
  and nothing references it — verified: 0 `SDL3-static` references in the OFF
  build graph.
- **Internal target.** `render2d_present_host_support` (INTERFACE) links
  `Render2D::Render2D` + `SDL3::SDL3-static` + `Vulkan::Vulkan`. SDL's headers
  are promoted to `INTERFACE_SYSTEM_INCLUDE_DIRECTORIES` so they do not trip
  Render2D's `-Werror` — the same isolation pattern as
  `render2d_font_runtime_support` and `render2d_thread_runtime_support`. The
  target is **never** linked or included by the umbrella `Render2D.hpp`.
- **Header location.** Present-host headers live under `include/Render2D/Present/`
  (analogous to `include/Render2D/Font/`) and require linking the internal
  target; they are never reachable from the umbrella. SDL `#include`s appear in
  exactly one place — `include/Render2D/Present/PresentHost.hpp`.
- **Runtimes unchanged.** `VulkanSwapchainRuntime` / `VulkanPresentRuntime` are
  not modified. SDL3 becomes one concrete provider of the `VkSurfaceKHR` they
  already consume, fully replaceable by the host engine. So this does **not**
  break red line #11: the present-host is a *default* host for standalone
  builds, not an ownership grab.
- **SDL build configuration.** Static only (`SDL_SHARED OFF`, `SDL_STATIC ON`),
  video + Vulkan kept (`SDL_VIDEO`, `SDL_VULKAN` ON); audio, render (SDL's own
  2D/GPU renderers), camera, joystick, haptic, hidapi, power, sensor, dialog,
  tray, notification, OpenGL/ES, tests, examples, and install are disabled. The
  resulting backends are video drivers `dummy offscreen windows` only.
- **Scope guard.** The present-host is a thin SDL3↔Vulkan presentation shell
  (window + surface + swapchain lifecycle + present-loop driver). It must not
  grow input/audio/scene/asset/gameplay subsystems — those remain the host
  engine's job and out of scope for this module.

Note on SDL's own `CLAUDE.md` ("AI must not be used to generate code for
contributions to this project"): that governs contributions *to* SDL. Render2D
only *consumes* SDL as a pinned, unmodified dependency — no file under
`third_party/sdl/` is edited and nothing is contributed upstream — so the
directive is respected.

## Consequences

- Cloning now requires `git submodule update --init --recursive` for a
  present-host build to configure; without it `third_party/sdl` is empty and
  `add_subdirectory` fails. The `RENDER2D_BUILD_PRESENT_HOST=OFF` build is
  unaffected and needs no SDL checkout.
- There are now four `third_party/` submodules. SDL adds compile time (the SDL3
  static lib) to a present-host build; the OFF build is unchanged.
- SDL3 is zlib-licensed (permissive) — unlike the FreeType FTL/GPL question, it
  carries no release-blocking license decision.
- The pin moves in lockstep with any SDL API used under
  `include/Render2D/Present/`; a bump is an explicit submodule checkout +
  re-running the Debug + Perf gates.
- SDL is built with its own ISA flags and does not link `Render2D`, so the perf
  preset's `-mavx2 -mfma` (Stage 24) does not propagate into SDL's
  translation units.
- 22A is plumbing only. The `PresentHost.hpp` API here just queries the compiled
  vs. linked SDL version (`presentHostCompiledSdlVersion` /
  `presentHostLinkedSdlVersion`) to prove the submodule builds, the target
  links, and the headers are reachable, touching no window/display/GPU so it is
  headless-CI-safe. The real surface/swapchain/present work is 22B–22E.

## Verification

- Debug `ctest` 58/58, Perf `ctest` 79/79 (each +1: the new
  `render2d.present_host_smoke`, which reports `SDL compiled 3.4.10, linked
  3.4.10`).
- `RENDER2D_BUILD_PRESENT_HOST=OFF` configures and builds with SDL fully absent
  from the build graph (0 `SDL3-static` references) and the present-host test
  unregistered.
- Umbrella purity: SDL `#include`s exist only in
  `include/Render2D/Present/PresentHost.hpp`; `Render2D.hpp` references neither
  SDL nor `Present/`.
- Constraint scan 3/3 (`third_party/` is excluded by design),
  `clang-tidy --verify-config` clean, the new TU clang-tidy clean.

## Update — Stage 22B (2026-06-16)

22B realizes the reusable present-host the 22A plumbing anticipated, within the
same decision (no contract change). The `PresentHost` RAII class — in the same
`include/Render2D/Present/PresentHost.hpp`, still the only SDL `#include` in the
tree — owns the SDL video subsystem + window lifetime and turns a host-provided
`VkInstance` into a real `VkSurfaceKHR`: `initialize` (`SDL_Init` video + a
Vulkan window), `requiredInstanceExtensions` (`SDL_Vulkan_GetInstanceExtensions`),
`createSurface` (`SDL_Vulkan_CreateSurface`), `presentationSupport`
(`SDL_Vulkan_GetPresentationSupport`), `pixelSize`, `destroySurface`, `shutdown`.

It still does **not** create the `VkInstance`/`VkDevice` — those remain the
host's, preserving red line #11 — and `VulkanSwapchainRuntime` /
`VulkanPresentRuntime` are unchanged: SDL's surface is fed straight into the
existing `createSwapchain`. So SDL3 is confirmed as one replaceable surface
provider, not an ownership grab.

The window is opened **hidden** so the smoke test does not flash a window during
`ctest`; a hidden Vulkan window still yields a valid surface/swapchain on desktop
drivers. The surface-capabilities output struct is left uninitialized and filled
by the driver (rather than `{}`-initialized) to avoid a clang-tidy
`bugprone-invalid-enum-default-initialization` false positive on
`VkSurfaceTransformFlagBitsKHR` — no `NOLINT`, matching the repo's
suppression-free convention.

### Verification (22B)

- Debug `ctest` 59/59, Perf `ctest` 80/80 (each +1: the new
  `render2d.present_host_window_smoke`). On a GPU+display machine the test runs
  the real path end to end (window → instance with SDL's extensions → surface →
  present-capable device + `VK_KHR_swapchain` → `createSwapchain` → resolve
  native handles → release): observed "surface+swapchain OK, 2 images, 640x480,
  format 44" (`VK_FORMAT_B8G8R8A8_UNORM`). Every bring-up failure is a graceful
  skip (returns 0), so it stays green on headless CI.
- `RENDER2D_BUILD_PRESENT_HOST=OFF` still configures and builds the whole tree
  (291/291) with SDL absent (0 `SDL3-static` / `third_party/sdl` / `present_host`
  references) and both present-host tests unregistered.
- Umbrella purity unchanged (SDL only in `PresentHost.hpp`); constraint scan 3/3;
  `clang-tidy --verify-config` clean and the new TU clang-tidy clean.

## Update — Stage 22C (2026-06-17)

22C puts the 22B surface/swapchain to work: it presents *real frames* through the
unchanged Vulkan runtimes and exercises the resize/recreate path — again with no
contract change. The new `render2d.present_loop_smoke` runs the full per-frame
loop on the live SDL window:

    waitFence → acquireNextImage (signals image_available) → record (clear the
    swapchain image to a solid colour) → submit (waits image_available, signals
    render_finished + the in-flight fence) → present (waits render_finished).

The resize path is a swapchain recreate that threads the retiring swapchain in as
`old_swapchain`, then releases the old state — the exact sequence a real window
resize triggers via `VK_ERROR_OUT_OF_DATE_KHR` → `SwapchainOutOfDate`. The loop
handles that status from acquire/present and additionally *forces* one recreate
mid-run, so the recreate→release machinery is covered deterministically (a hidden
window never spontaneously resizes).

Still no new ownership: `VulkanSwapchainRuntime` / `VulkanSyncRuntime` /
`VulkanCommandRuntime` / `VulkanSubmitRuntime` / `VulkanPresentRuntime` are all
unchanged and merely consume the SDL surface, and `PresentHost` is unchanged from
22B. The instance/device/format bring-up that 22B's test wrote inline is extracted
to `tests/support/PresentBringup.hpp` and shared by both present tests (test-only
scaffolding, like `VulkanSmokeContext`) — the only refactor 22C makes to existing
code. Swapchain image usage adds `TRANSFER_DST` only when the surface advertises
it (`supportedUsageFlags`); otherwise the frame is a valid blank present, so the
loop path is proven everywhere.

### Verification (22C)

- Debug `ctest` 60/60, Perf `ctest` 81/81 (each +1: `render2d.present_loop_smoke`).
  On a GPU+display machine the real path ran end to end: "presented 6 frame(s)
  across 2 swapchain generation(s), 640x480, cleared" (initial swapchain + one
  forced recreate). Every bring-up failure is a graceful skip (returns 0), so it
  stays green on headless CI.
- `RENDER2D_BUILD_PRESENT_HOST=OFF` still builds the whole tree (291/291) with SDL
  absent (no `SDL3` artifacts; only the option's own cache description mentions
  it) and all three present tests unregistered (57 tests).
- Umbrella purity unchanged (SDL only in `PresentHost.hpp`); constraint scan 3/3;
  the new + refactored present TUs clang-tidy clean.

## Update — Stage 22D (2026-06-17)

22D closes the loop opened by 22C: it proves that what reaches the on-screen
(swapchain) image is byte-identical to the trusted offscreen render baseline —
a visible-capture regression guard — again with no contract change. The new
`render2d.present_visible_capture` does it all in one command buffer / one
submission on the live SDL surface:

1. render a deterministic gradient full-screen triangle into an offscreen image
   (`VulkanDynamicRenderEncoder`), created in the swapchain's exact format;
2. copy that offscreen image to a readback buffer → the **offscreen baseline**
   (the very path every other `vulkan_*` render test trusts);
3. copy the offscreen image onto the acquired swapchain image;
4. copy the swapchain image back to a second readback buffer → the **capture**;
5. transition the swapchain image to `PRESENT_SRC` and present it.

After the fence, it asserts capture == baseline byte-for-byte. Matching the
offscreen target's format to the swapchain's makes the image→image copy a
byte-exact transfer (no conversion), so a mismatch can only come from a real
copy/layout/region defect. A `gl_FragCoord`-keyed, channel-asymmetric gradient
frag (new `kGradientFragSpv`) is used precisely so that compare also catches a
wrong copy region/extent/orientation that a uniform fill would hide.

Why copy rather than render straight to the swapchain: the encoders resolve an
`ImageRef` owned by `VulkanResourceRuntime`, and a swapchain image is not one —
rendering directly would mean changing a runtime contract, which Stage 22 has
deliberately never done. So the rendered content reaches the swapchain via a raw
`vkCmdCopyImage` on resolved handles, the same windowless-core-preserving idiom
22C uses for its raw barriers/clears. All five runtimes and `PresentHost` are
unchanged (red line #11 intact).

The one new wrinkle is that 22D's baseline render needs **dynamic rendering**,
which 22B/22C did not. The shared `tests/support/PresentBringup.hpp` therefore
gained *additive* render-capable helpers — `createPresentRenderInstance`
(requests Vulkan 1.3 when available), `presentDeviceSupportsDynamicRendering`,
and `createPresentRenderDevice` (enables the `dynamicRendering` feature) —
mirroring `VulkanSmokeContext`. The 22B/22C `createPresentInstance` /
`createPresentDevice` (Vulkan 1.0 + `VK_KHR_swapchain`) are byte-for-byte
unchanged. The swapchain images additionally request `TRANSFER_DST | TRANSFER_SRC`
usage (copy target + readback source); if the device lacks dynamic rendering or
the surface does not advertise that usage, the test skips gracefully.

### Verification (22D)

- Debug `ctest` 61/61, Perf `ctest` 82/82 (each +1: `render2d.present_visible_capture`).
  On a GPU+display machine the real path ran end to end and reported
  "640x480 visible capture == offscreen baseline (1228800 bytes)". Every
  bring-up failure is a graceful skip (returns 0), so it stays green on headless CI.
- `RENDER2D_BUILD_PRESENT_HOST=OFF` still builds the whole tree (291/291) with SDL
  absent (no `SDL3` artifacts, no present-host executables) and all four present
  tests unregistered (57 tests; verified a list diff drops exactly the four SDL
  present-host tests and nothing else).
- Umbrella purity unchanged (SDL only in `PresentHost.hpp`); constraint scan 3/3;
  the new + dependent present TUs clang-tidy clean.

## Update — Stage 22E (2026-06-17)

22E closes Stage 22: it makes the on-screen present path programmatically
RenderDoc-capturable and wires the closeout. A real gradient frame is rendered to
the live swapchain and its submit+present is wrapped in RenderDoc
`StartFrameCapture`/`EndFrameCapture`, producing a repeatable `.rdc` a developer
can open to inspect the draw call, pipeline, shaders, the copy onto the swapchain
image, and the present. As with 22B–22D there is no runtime contract change: all
five Vulkan runtimes and `PresentHost` are unchanged, and the rendered content
reaches the swapchain by the 22D idiom (render offscreen via
`VulkanDynamicRenderEncoder` → raw `vkCmdCopyImage` onto the acquired swapchain
image → present), since the encoders resolve a resource `ImageRef` and a swapchain
image is not one. Red line #11 (the host owns the window + surface; the
present-host creates no `VkInstance`/`VkDevice`) stays intact.

New dependency — a *vendored single header*, not a submodule:
`third_party/renderdoc/renderdoc_app.h` is the official RenderDoc in-application
API header (MIT; the license banner is in the file). It carries no build and no
library — RenderDoc is never linked. It is promoted to a SYSTEM include on
`render2d_present_host_support`, reachable as `<renderdoc_app.h>` only when the
present-host option is ON; a `RENDER2D_BUILD_PRESENT_HOST=OFF` build never puts it
on any include path, so that build is RenderDoc-free as well as SDL-free. See
`third_party/renderdoc/README.md` for provenance.

New capability — `include/Render2D/Present/RenderDocCapture.hpp`: a thin,
non-owning wrapper over the RenderDoc API, isolated exactly like `PresentHost.hpp`
(behind the internal target, never in the umbrella). It only *attaches* to an
ALREADY-INJECTED RenderDoc — `GetModuleHandleA("renderdoc.dll")` on Windows,
`dlopen(..., RTLD_NOLOAD)` on Unix — and never loads the module itself (loading it
fresh would not hook the already-created instance/device and is unsupported by
RenderDoc). When RenderDoc is absent — a normal run or headless CI — `attach()`
returns false and every capture call is a no-op, so wrapping a present frame is
always safe.

Scope-guard amendment: per the decision to make capture a *reusable* present-host
capability (not throwaway test scaffolding), the present-host's admissible scope
now explicitly includes **frame-capture / diagnostics integration (RenderDoc)** —
a presentation-debugging concern, the only tooling category admitted beyond
window/surface/swapchain/present. The original exclusions stand unchanged: input,
audio, scene/asset, gameplay/scripting subsystems remain the host engine's job and
out of scope for this module.

Verification honesty: `ctest` runs the test WITHOUT RenderDoc, so it proves the
present frame succeeds and the capture wrap is a clean no-op; the actual `.rdc`
artifact is produced only when the exe is launched under RenderDoc — the manual
check below (the established GPU/display-gated + manual-visible pattern).

### RenderDoc manual-capture checklist

1. Build the present-host target (`RENDER2D_BUILD_PRESENT_HOST=ON`, default).
2. Launch `render2d_present_renderdoc_capture_smoke.exe` from the RenderDoc UI (or
   `renderdoccmd capture`), so `renderdoc.dll` is injected.
3. Expect stdout `RenderDoc API <maj.min.patch> attached`, then
   `... gradient frame presented + captured`.
4. Confirm one `.rdc` was written (path template `render2d_present_22e`) and
   `GetNumCaptures` incremented; open it and verify the gradient draw call, the
   graphics pipeline + shaders, the copy onto the swapchain image, and the present.

### Verification (22E)

- Debug `ctest` 62/62, Perf `ctest` 83/83 (each +1: the new
  `render2d.present_renderdoc_capture`). On a GPU+display machine the real path
  ran end to end and reported "640x480 gradient frame presented (no RenderDoc)"
  (RenderDoc is not injected under `ctest`, so capture is a no-op by design).
  Every bring-up failure is a graceful skip (returns 0), so it stays green on
  headless CI.
- `RENDER2D_BUILD_PRESENT_HOST=OFF` still builds the whole tree (291/291) with both
  SDL and RenderDoc absent (0 `SDL3-static` / `renderdoc_app` references in the
  build graph) and all five present tests unregistered (57 tests; a list diff
  drops exactly the five SDL present-host tests and nothing else).
- Umbrella purity unchanged (SDL only in `PresentHost.hpp`, `renderdoc_app.h` only
  in `RenderDocCapture.hpp`, neither in `Render2D.hpp`); constraint scan 3/3; the
  new TU + `RenderDocCapture.hpp` clang-tidy clean; `git diff --check` clean.
