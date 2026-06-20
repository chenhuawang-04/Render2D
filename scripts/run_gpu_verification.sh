#!/usr/bin/env bash
# Real-GPU verification: build the full suite and run it with the capability gates
# ARMED, so a green run is positive proof the GPU (and, if a display is present,
# the on-screen present) paths actually executed -- not just that they skipped.
#
# Why this exists: every render2d.vulkan_* / present_* test graceful-skips (returns
# 0) when no device/display is available, so a plain `ctest` can be green without
# ever touching the GPU (see the README "No GPU required"). Setting
# RENDER2D_REQUIRE_GPU=1 arms render2d.gpu_presence_gate and RENDER2D_REQUIRE_PRESENT=1
# arms render2d.present_capability_gate to HARD-FAIL when the capability is absent;
# because a present device makes every other GPU/present test run its real path,
# those two gates turn the whole suite's green into a guarantee.
#
# Run this on a machine with a Vulkan GPU (and, for present, a display). On a
# headless box it will (correctly) fail the armed gates. Knobs:
#   R2D_REQUIRE_GPU=0      do not arm the GPU gate
#   R2D_REQUIRE_PRESENT=0  do not arm the present gate (set this on a GPU server
#                          with no display, where the present tests cannot run)
#   R2D_PRESETS="a b"      which CMake presets to build/test (default: Debug + Perf)
#
# Used non-interactively by .github/workflows/gpu.yml on a self-hosted GPU runner.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

require_gpu="${R2D_REQUIRE_GPU:-1}"
require_present="${R2D_REQUIRE_PRESENT:-1}"
read -r -a presets <<< "${R2D_PRESETS:-clang-ninja-debug clang-ninja-perf}"

echo "Render2D GPU verification"
echo "  RENDER2D_REQUIRE_GPU=$require_gpu  RENDER2D_REQUIRE_PRESENT=$require_present"
echo "  presets: ${presets[*]}"
echo

for preset in "${presets[@]}"; do
    echo "=== configure + build: $preset ==="
    cmake --preset "$preset"
    cmake --build --preset "$preset"

    echo "=== ctest (gates armed): $preset ==="
    RENDER2D_REQUIRE_GPU="$require_gpu" RENDER2D_REQUIRE_PRESENT="$require_present" \
        ctest --preset "$preset"
    echo
done

echo "GPU verification complete (${presets[*]}, gates armed)."
