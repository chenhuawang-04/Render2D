// render2d.gpu_presence_gate -- a capability gate that (under RENDER2D_REQUIRE_GPU)
// turns the whole suite's "green" into proof the GPU paths actually ran.
//
// Every render2d.vulkan_* smoke test funnels through createVulkanSmokeContext()
// (tests/support/VulkanSmokeContext.hpp): when it cannot create an instance +
// device the test returns 0 (a graceful skip). So a green `ctest` on a box with
// no GPU proves only the contract/state logic -- NOT that any GPU path executed
// (see the README "No GPU required"). This canary closes that gap WITHOUT touching
// the existing tests: because a present device makes every one of them run its
// real path, asserting "a device exists" here is sufficient.
//
//   * Default (flag unset): create the context if possible, print what was found,
//     return 0 either way -- a no-op on headless CI, exactly like the others.
//   * RENDER2D_REQUIRE_GPU set: a missing instance/device is a HARD FAILURE
//     (return 1). A green run under this flag => a device was present => every
//     createVulkanSmokeContext-based test executed its GPU path this run.
//
// scripts/run_gpu_verification.{sh,ps1} and .github/workflows/gpu.yml arm the flag
// for a real-device pass (locally or on a self-hosted GPU runner). Capability-
// specific paths (bindless / descriptor-indexing, the validation layer) stay gated
// on those features, not just device presence -- this canary reports them but does
// not require them.

#include "support/EnvRequire.hpp"
#include "support/VulkanSmokeContext.hpp"

#include <Render2D/Render2D.hpp>

#include <vulkan/vulkan.h>

#include <cstdio>
#include <exception>
#include <iostream>

namespace {

[[nodiscard]] int runGate()
{
    const bool require_gpu = Render2DTest::environmentFlagEnabled(Render2DTest::kRequireGpuEnv);

    Render2DTest::VulkanSmokeContext context{};
    if (!Render2DTest::createVulkanSmokeContext(context)) {
        if (require_gpu) {
            std::cout << "gpu-presence-gate: RENDER2D_REQUIRE_GPU is set but no Vulkan "
                         "instance/device could be created -- FAILING\n";
            return 1;
        }
        std::cout << "gpu-presence-gate: no Vulkan device available, skipping "
                     "(set RENDER2D_REQUIRE_GPU=1 to make this a hard failure)\n";
        return 0;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context.physical_device, &properties);
    std::cout << "gpu-presence-gate: device \"" << properties.deviceName << "\" (Vulkan "
              << VK_API_VERSION_MAJOR(context.api_version) << "." << VK_API_VERSION_MINOR(context.api_version) << "."
              << VK_API_VERSION_PATCH(context.api_version) << "), dynamic_rendering="
              << (context.supports_dynamic_rendering ? "yes" : "no")
              << ", descriptor_indexing=" << (context.supports_descriptor_indexing ? "yes" : "no")
              << ", bindless=" << (context.supports_bindless ? "yes" : "no") << "\n";
    if (require_gpu) {
        std::cout << "gpu-presence-gate: RENDER2D_REQUIRE_GPU satisfied -- the GPU paths run this pass\n";
    }

    Render2DTest::destroyVulkanSmokeContext(context);
    return 0;
}

} // namespace

int main()
{
    try {
        return runGate();
    } catch (const std::exception& exception) {
        std::fputs("gpu_presence_gate exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("gpu_presence_gate unknown exception\n", stderr);
        return 1;
    }
}
