#pragma once

// Stage 22A: SDL3 present-host support -- build/link foundation.
//
// This header is part of the optional present-host subsystem and links SDL3. It
// is NOT included by the public umbrella `Render2D.hpp`; consumers link the
// internal `render2d_present_host_support` target (the same isolation pattern as
// `render2d_font_runtime_support` and `render2d_thread_runtime_support`).
//
// Render2D's core stays windowless. The present-host exists only so a test/dev
// harness -- or a standalone build -- can obtain a real window and a
// `VkSurfaceKHR` (via SDL) to feed the existing `VulkanSwapchainRuntime`, which
// already takes a host-provided surface as input. A host engine sets
// `RENDER2D_BUILD_PRESENT_HOST=OFF` at merge and supplies its own surface, so
// nothing here ever reaches production. See
// docs/adr/2026-06-16-stage22-sdl3-present-host.md.
//
// 22A is build plumbing only: it proves the SDL submodule builds as a static
// library, that the internal target links it, and that the SDL headers are
// reachable through the target. The real window/surface/swapchain API arrives in
// 22B. The two queries below touch no window, display, or GPU, so they are safe
// to run in headless CI.

#include <SDL3/SDL.h>
#include <SDL3/SDL_version.h>

namespace Render2D {

// A semantic SDL version triple (major.minor.patch).
struct PresentHostSdlVersion
{
    int major;
    int minor;
    int patch;
};

// The SDL version the present-host headers were *compiled* against, read from
// the SDL headers. Proves the SDL include path is wired through the target.
[[nodiscard]] inline PresentHostSdlVersion presentHostCompiledSdlVersion() noexcept
{
    return PresentHostSdlVersion{
        SDL_MAJOR_VERSION,
        SDL_MINOR_VERSION,
        SDL_MICRO_VERSION,
    };
}

// The SDL version the process is *linked* against, from `SDL_GetVersion()`.
// Proves the SDL static library is actually linked. Creates no window/display/
// GPU resources, so it is safe in headless CI.
[[nodiscard]] inline PresentHostSdlVersion presentHostLinkedSdlVersion() noexcept
{
    const int packed = SDL_GetVersion();
    return PresentHostSdlVersion{
        SDL_VERSIONNUM_MAJOR(packed),
        SDL_VERSIONNUM_MINOR(packed),
        SDL_VERSIONNUM_MICRO(packed),
    };
}

}  // namespace Render2D
