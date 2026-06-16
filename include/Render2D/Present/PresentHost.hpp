#pragma once

// Stage 22 present-host support -- SDL3-backed window + Vulkan surface provider.
//
// This header is part of the optional present-host subsystem and links SDL3. It
// is NOT included by the public umbrella `Render2D.hpp`; consumers link the
// internal `render2d_present_host_support` target (the same isolation pattern as
// `render2d_font_runtime_support` and `render2d_thread_runtime_support`). SDL
// `#include`s appear in this file and nowhere else in the tree.
//
// Render2D's core stays windowless. The present-host exists only so a test/dev
// harness -- or a standalone build -- can obtain a real window and a
// `VkSurfaceKHR` (via SDL) to feed the existing `VulkanSwapchainRuntime`, which
// already takes a host-provided surface as input. The present-host deliberately
// does NOT create the `VkInstance`/`VkDevice`: those stay the host's
// responsibility (ProjectMergeTODO red line #11 -- the host owns the window +
// surface). A host engine sets `RENDER2D_BUILD_PRESENT_HOST=OFF` at merge and
// supplies its own surface, so nothing here ever reaches production. See
// docs/adr/2026-06-16-stage22-sdl3-present-host.md.
//
// 22A added the build plumbing (the version queries below prove the submodule
// builds, the target links, and the headers are reachable). 22B adds the
// reusable `PresentHost`: it opens an SDL Vulkan window, reports the instance
// extensions SDL needs, and turns a host `VkInstance` into a real
// `VkSurfaceKHR`. Window/surface creation gracefully fails (returns false) on a
// headless box with no usable video driver, so the smoke test skips cleanly in
// CI.

#include "Render2D/Core/Types.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_version.h>
#include <SDL3/SDL_vulkan.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <span>

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

// A reusable SDL3-backed present-host: owns the SDL video subsystem + window
// lifetime and produces a `VkSurfaceKHR` for a host-provided `VkInstance`.
//
// Ownership/teardown contract: the surface is created against a caller-owned
// `VkInstance` and must be destroyed *before* that instance. `destroySurface()`
// (and the destructor, which calls it) tear the surface down using the instance
// it was created with, so the correct shutdown order is:
//   swapchain runtime shutdown -> present_host surface/window teardown ->
//   vkDestroyDevice -> vkDestroyInstance
// i.e. call `shutdown()` (or `destroySurface()`) before `vkDestroyInstance`.
//
// Like the Vulkan runtimes, this is a non-copyable, non-movable RAII type (it
// owns process-global SDL state and OS handles).
class PresentHost
{
public:
    PresentHost() = default;
    PresentHost(const PresentHost&) = delete;
    PresentHost& operator=(const PresentHost&) = delete;
    PresentHost(PresentHost&&) = delete;
    PresentHost& operator=(PresentHost&&) = delete;

    ~PresentHost() noexcept
    {
        shutdown();
    }

    // Initialize SDL video and open a Vulkan-capable window. Pass `hidden_ =
    // true` for automated tests so no window flashes on screen (a hidden window
    // still yields a valid surface/swapchain on desktop drivers). Returns false
    // (and records `lastError()`) when SDL video init or window creation fails,
    // e.g. on a headless CI box -- callers treat that as a graceful skip.
    [[nodiscard]] bool initialize(const char* title_, int width_, int height_, bool hidden_) noexcept
    {
        if (window != nullptr)
        {
            last_error = "present-host already initialized";
            return false;
        }
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            captureError();
            return false;
        }
        owns_sdl_init = true;

        SDL_WindowFlags flags = SDL_WINDOW_VULKAN;
        if (hidden_)
        {
            flags |= SDL_WINDOW_HIDDEN;
        }
        window = SDL_CreateWindow(title_, width_, height_, flags);
        if (window == nullptr)
        {
            captureError();
            shutdown();
            return false;
        }
        return true;
    }

    // The Vulkan instance extensions SDL needs enabled to create a surface for
    // this window, suitable for `VkInstanceCreateInfo::ppEnabledExtensionNames`.
    // The returned array is owned by SDL -- do not free it. Empty on failure.
    [[nodiscard]] std::span<const char* const> requiredInstanceExtensions() const noexcept
    {
        Uint32 count = 0U;
        const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
        if (names == nullptr)
        {
            return {};
        }
        return std::span<const char* const>(names, static_cast<std::size_t>(count));
    }

    // Create the window's `VkSurfaceKHR` for `instance_`, which must have been
    // created with `requiredInstanceExtensions()` enabled. The host-owned
    // instance is remembered so `destroySurface()` can tear the surface down
    // before the caller destroys the instance. Returns false on failure.
    [[nodiscard]] bool createSurface(VkInstance instance_) noexcept
    {
        if (window == nullptr || instance_ == VK_NULL_HANDLE || surface != VK_NULL_HANDLE)
        {
            last_error = "present-host createSurface preconditions not met";
            return false;
        }
        VkSurfaceKHR new_surface = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(window, instance_, nullptr, &new_surface))
        {
            captureError();
            return false;
        }
        instance = instance_;
        surface = new_surface;
        return true;
    }

    // Destroy the surface (if any) using the instance it was created with. Must
    // run before the caller destroys that `VkInstance`. Idempotent.
    void destroySurface() noexcept
    {
        if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE)
        {
            SDL_Vulkan_DestroySurface(instance, surface, nullptr);
        }
        surface = VK_NULL_HANDLE;
        instance = VK_NULL_HANDLE;
    }

    // Whether `queue_family_index_` on `physical_device_` can present to this
    // platform's window-system surfaces under `instance_`. Use this (together
    // with graphics support) to pick a present-capable queue family.
    [[nodiscard]] bool presentationSupport(
        VkInstance instance_,
        VkPhysicalDevice physical_device_,
        U32 queue_family_index_) const noexcept
    {
        return SDL_Vulkan_GetPresentationSupport(
            instance_,
            physical_device_,
            static_cast<Uint32>(queue_family_index_));
    }

    // The window's drawable size in pixels -- the extent a swapchain should use.
    // Returns false (with zeroed outputs) if the size is unavailable or zero
    // (e.g. a minimized window).
    [[nodiscard]] bool pixelSize(U32& out_width_, U32& out_height_) const noexcept
    {
        out_width_ = 0U;
        out_height_ = 0U;
        if (window == nullptr)
        {
            return false;
        }
        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSizeInPixels(window, &width, &height))
        {
            return false;
        }
        if (width <= 0 || height <= 0)
        {
            return false;
        }
        out_width_ = static_cast<U32>(width);
        out_height_ = static_cast<U32>(height);
        return true;
    }

    // Tear down the surface (if live), the window, and the SDL video subsystem,
    // in that order. Safe to call repeatedly.
    void shutdown() noexcept
    {
        destroySurface();
        if (window != nullptr)
        {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        if (owns_sdl_init)
        {
            SDL_Quit();
            owns_sdl_init = false;
        }
    }

    [[nodiscard]] bool isWindowOpen() const noexcept
    {
        return window != nullptr;
    }

    [[nodiscard]] VkSurfaceKHR surfaceHandle() const noexcept
    {
        return surface;
    }

    [[nodiscard]] SDL_Window* windowHandle() const noexcept
    {
        return window;
    }

    // The most recent SDL/precondition error message, or "" if none. The string
    // is owned by SDL and only valid until the next SDL call.
    [[nodiscard]] const char* lastError() const noexcept
    {
        return last_error;
    }

private:
    void captureError() noexcept
    {
        last_error = SDL_GetError();
    }

    SDL_Window* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const char* last_error = "";
    bool owns_sdl_init = false;
};

}  // namespace Render2D
