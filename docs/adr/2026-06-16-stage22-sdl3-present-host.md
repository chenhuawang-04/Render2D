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
