#pragma once

// Stage 22E present-host capability -- programmatic RenderDoc frame capture.
//
// Like PresentHost.hpp, this header is part of the optional present-host
// subsystem and is NOT included by the public umbrella `Render2D.hpp`; consumers
// link the internal `render2d_present_host_support` target, which puts the
// vendored `third_party/renderdoc/renderdoc_app.h` on the include path. See
// docs/adr/2026-06-16-stage22-sdl3-present-host.md (Stage 22E).
//
// `RenderDocCapture` is a thin, non-owning wrapper over the RenderDoc
// in-application API. It only ever *attaches* to an ALREADY-INJECTED RenderDoc
// (running the program under RenderDoc, or after injection): on Windows via
// `GetModuleHandleA("renderdoc.dll")`, on Unix via `dlopen(..., RTLD_NOLOAD)`.
// It never *loads* the module itself -- loading renderdoc.dll fresh would not
// hook the already-created VkInstance/VkDevice and is unsupported by RenderDoc.
//
// When RenderDoc is absent (the common case -- a normal run or headless CI),
// `attach()` returns false and every capture call is a no-op, so wrapping a
// present frame in start/endFrameCapture is always safe. The actual `.rdc`
// artifact is produced only when the program runs under RenderDoc; that is the
// "manual capture" verification in the Stage 22E checklist.
//
// This integrates frame-capture/diagnostics into the present-host on purpose:
// it is a presentation-debugging concern, the only category of tooling the
// present-host's scope guard admits beyond window/surface/swapchain/present. It
// does NOT pull in input/audio/scene/asset/gameplay subsystems -- those remain
// the host engine's responsibility.

#include "Render2D/Core/Types.hpp"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#  include <dlfcn.h>
#endif

#include <renderdoc_app.h>

namespace Render2D {

// Non-owning handle to an injected RenderDoc's in-application API. Default
// special members are fine: the wrapped pointer is owned by RenderDoc, not by
// this object, so copying just shares the (process-global) API pointer.
class RenderDocCapture
{
public:
    RenderDocCapture() = default;

    // Attach to an already-injected RenderDoc. Idempotent; cheap and safe to call
    // when RenderDoc is absent (returns false, leaves this unavailable). Returns
    // available().
    [[nodiscard]] bool attach() noexcept
    {
        if (api != nullptr)
        {
            return true;
        }

        pRENDERDOC_GetAPI get_api = nullptr;
#if defined(_WIN32)
        const HMODULE module = GetModuleHandleA("renderdoc.dll");
        if (module == nullptr)
        {
            return false;
        }
        get_api = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"));
#elif defined(__unix__) || defined(__APPLE__)
        // RTLD_NOLOAD: resolve the handle only if librenderdoc is already loaded
        // (i.e. RenderDoc injected it); never load it ourselves.
        void* const module = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
        if (module == nullptr)
        {
            return false;
        }
        get_api = reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(module, "RENDERDOC_GetAPI"));
#endif
        if (get_api == nullptr)
        {
            return false;
        }

        RENDERDOC_API_1_6_0* new_api = nullptr;
        if (get_api(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void**>(&new_api)) != 1 ||
            new_api == nullptr)
        {
            return false;
        }
        api = new_api;
        return true;
    }

    [[nodiscard]] bool available() const noexcept
    {
        return api != nullptr;
    }

    // RenderDoc's running API version (major.minor.patch); 0.0.0 when not attached.
    void apiVersion(int& out_major_, int& out_minor_, int& out_patch_) const noexcept
    {
        out_major_ = 0;
        out_minor_ = 0;
        out_patch_ = 0;
        if (api != nullptr)
        {
            api->GetAPIVersion(&out_major_, &out_minor_, &out_patch_);
        }
    }

    // Set the on-disk path template for written `.rdc` captures (e.g.
    // "build/render2d_present"). No-op when RenderDoc is absent.
    void setCaptureFilePathTemplate(const char* path_template_) noexcept
    {
        if (api != nullptr && path_template_ != nullptr)
        {
            api->SetCaptureFilePathTemplate(path_template_);
        }
    }

    // Begin a frame capture. Null device + window (the default) tells RenderDoc to
    // capture all activity on the active window until endFrameCapture -- the
    // simplest, most robust mode for a single-surface present. No-op when absent.
    void startFrameCapture(
        RENDERDOC_DevicePointer device_ = nullptr,
        RENDERDOC_WindowHandle window_ = nullptr) noexcept
    {
        if (api != nullptr)
        {
            api->StartFrameCapture(device_, window_);
        }
    }

    // End the frame capture started by startFrameCapture. Returns true if a
    // capture was actually written (always false when RenderDoc is absent).
    [[nodiscard]] bool endFrameCapture(
        RENDERDOC_DevicePointer device_ = nullptr,
        RENDERDOC_WindowHandle window_ = nullptr) noexcept
    {
        if (api == nullptr)
        {
            return false;
        }
        return api->EndFrameCapture(device_, window_) == 1U;
    }

    // Number of `.rdc` captures RenderDoc has written so far (0 when absent).
    [[nodiscard]] U32 numCaptures() const noexcept
    {
        if (api == nullptr)
        {
            return 0U;
        }
        return static_cast<U32>(api->GetNumCaptures());
    }

private:
    RENDERDOC_API_1_6_0* api = nullptr;
};

}  // namespace Render2D
