// render2d.present_capability_gate -- the on-screen present counterpart of
// render2d.gpu_presence_gate. The render2d.present_* / window_scene_* tests
// graceful-skip at every WindowTestHarness bring-up gate on a headless/limited box
// (no video driver, no Vulkan ICD, no present-capable GPU, no dynamic rendering,
// no TRANSFER-capable swapchain), so a green run does not prove the present path
// executed. Under RENDER2D_REQUIRE_PRESENT this canary turns any such bring-up
// failure into a HARD FAILURE, so a green run with the flag set proves the present
// tests ran their real path.
//
// It brings the path up through the STRICT gate the capture/sprite present tests
// need -- Vulkan 1.3 + dynamicRendering window/surface/device, then a
// COLOR_ATTACHMENT|TRANSFER_DST|TRANSFER_SRC swapchain -- a superset of the 1.0
// clear-only path, so "ready here" implies every present test proceeds past its
// gates. It submits no frame; it only proves bring-up (the runtimes are exercised
// for real by the present tests themselves).
//
//   * Default (flag unset): graceful skip (return 0), a no-op on headless CI.
//   * RENDER2D_REQUIRE_PRESENT set: a failure to reach a TRANSFER-capable
//     swapchain is a HARD FAILURE (return 1).
//
// Gated on RENDER2D_BUILD_PRESENT_HOST (links render2d_present_host_support); a
// host-min / merge build (present-host OFF) omits it, like the other present tests.

#include "support/EnvRequire.hpp"
#include "support/WindowTestHarness.hpp"

#include <Render2D/Render2D.hpp>

#include <Render2D/Present/PresentHost.hpp>

#include <vulkan/vulkan.h>

#include <cstdio>
#include <exception>
#include <iostream>

namespace {

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using SwapchainRuntime = R2D::VulkanSwapchainRuntime<Provider, Dim>;
using ActiveSwapchain = Render2DTest::ActiveSwapchain<Provider, Dim>;

constexpr const char* kLogPrefix = "present-capability-gate";

// Create (and immediately release) a TRANSFER-capable swapchain on the harness's
// surface -- the last gate the capture/sprite present tests pass before rendering.
// Returns true only if that swapchain came up.
[[nodiscard]] bool swapchainComesUp(Render2DTest::WindowTestHarness& harness_)
{
    constexpr VkImageUsageFlags kRequiredUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!Render2DTest::surfaceSupportsUsage(harness_.physicalDevice(), harness_.surface(), kRequiredUsage, kLogPrefix)) {
        return false;
    }
    const R2D::U32 image_usage =
        static_cast<R2D::U32>(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) | static_cast<R2D::U32>(kRequiredUsage);

    SwapchainRuntime swapchain_runtime{};
    if (swapchain_runtime.initialize({.device = harness_.device()}).code != R2D::NativeStatusCode::Ok ||
        swapchain_runtime.reserveSwapchains(1U).code != R2D::NativeStatusCode::Ok ||
        swapchain_runtime.reserveSwapchainImages(Render2DTest::kWindowSwapchainImageCapacity).code !=
            R2D::NativeStatusCode::Ok) {
        std::cout << kLogPrefix << ": swapchain runtime init failed, skipping\n";
        return false;
    }

    ActiveSwapchain active{};
    if (Render2DTest::createCaptureSwapchain(
            harness_.physicalDevice(),
            harness_.host(),
            harness_.queueFamilyIndex(),
            image_usage,
            swapchain_runtime,
            active) != R2D::NativeStatusCode::Ok) {
        std::cout << kLogPrefix << ": swapchain unavailable (e.g. minimized), skipping\n";
        return false;
    }

    std::cout << kLogPrefix << ": present path ready -- " << active.extent.width << "x" << active.extent.height
              << " swapchain, format " << active.format << ", " << active.state.image_count << " images\n";
    (void)swapchain_runtime.releaseSwapchainState(active.state);
    return true;
}

[[nodiscard]] int runGate()
{
    const bool require_present = Render2DTest::environmentFlagEnabled(Render2DTest::kRequirePresentEnv);

    Render2DTest::WindowTestHarness harness{};
    if (harness.initialize({.window_title = "Render2D Present Capability Gate",
                            .log_prefix = kLogPrefix,
                            .width = 640,
                            .height = 480,
                            .require_dynamic_rendering = true}) != Render2DTest::WindowTestHarness::Status::Ready) {
        if (require_present) {
            std::cout << kLogPrefix << ": RENDER2D_REQUIRE_PRESENT is set but the present path "
                         "could not be brought up -- FAILING\n";
            return 1;
        }
        return 0; // graceful skip already logged by the harness
    }

    const bool ready = swapchainComesUp(harness);
    if (require_present && !ready) {
        std::cout << kLogPrefix << ": RENDER2D_REQUIRE_PRESENT is set but no TRANSFER-capable "
                     "swapchain could be created -- FAILING\n";
        return 1;
    }
    if (require_present) {
        std::cout << kLogPrefix << ": RENDER2D_REQUIRE_PRESENT satisfied -- the present tests run this pass\n";
    }
    return 0;
}

} // namespace

int main() noexcept
{
    try {
        return runGate();
    } catch (const std::exception& exception) {
        std::fputs("present_capability_gate exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("present_capability_gate unknown exception\n", stderr);
        return 1;
    }
}
