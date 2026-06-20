#pragma once

// Test-only helper: read an opt-in "require this capability" environment flag.
//
// Every render2d.vulkan_* / present_* smoke test graceful-skips (returns 0) when
// no device/display is present, so a green `ctest` on a headless box does NOT
// prove the GPU paths ran -- only the contract/state logic (see the README "No
// GPU required"). Setting one of these flags flips the corresponding capability
// gate from "skip" to "hard fail", so a green run WITH the flag set is positive
// proof the capability was exercised this run.
//
// The flags are read ONLY by the capability-gate canaries
// (render2d.gpu_presence_gate / render2d.present_capability_gate); the other tests
// are unchanged. That is sufficient: a device, once present, makes every
// createVulkanSmokeContext-based test run its real GPU path, so a single canary
// asserting "a device exists" turns the whole suite's green into a GPU-ran
// guarantee. scripts/run_gpu_verification.{sh,ps1} and .github/workflows/gpu.yml
// arm the flags for a real-device pass.
//
// A flag counts as set when the variable exists and is neither empty nor "0".

#include <cstdlib>
#include <cstring>

namespace Render2DTest {

[[nodiscard]] inline bool environmentFlagEnabled(const char* variable_name_) noexcept
{
    const char* const value = std::getenv(variable_name_);
    if (value == nullptr || value[0] == '\0') {
        return false;
    }
    return std::strcmp(value, "0") != 0;
}

// The capability render2d.gpu_presence_gate requires: a Vulkan instance + device.
inline constexpr const char* kRequireGpuEnv = "RENDER2D_REQUIRE_GPU";

// The capability render2d.present_capability_gate requires: a window + a
// present-capable device + a TRANSFER-capable swapchain (and a display).
inline constexpr const char* kRequirePresentEnv = "RENDER2D_REQUIRE_PRESENT";

} // namespace Render2DTest
