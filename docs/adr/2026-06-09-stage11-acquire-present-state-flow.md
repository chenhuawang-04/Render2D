# ADR: Stage 11 Acquire to Present State Flow

Status: Accepted

Date: 2026-06-09

## Context

Stage 11C connected `SwapchainState` and `FrameSync` to Vulkan acquire/present. The remaining state-level gap is the ECS-side handoff from an acquired image to a present command, plus deterministic coverage for out-of-date result classification without requiring a platform window in default CTest.

## Decision

Add:

```text
include/Render2D/System/PresentSystem.hpp
```

`PresentCommandBuildSystem` converts `AcquiredImage[]` into `PresentCommand[]`. It is a pure component-stream transform and does not allocate or retain ECS storage.

Update `VulkanPresentRuntime` so:

- acquire/present result mapping is exposed through `mapVulkanAcquirePresentResult`;
- `VK_ERROR_OUT_OF_DATE_KHR` and `VK_ERROR_SURFACE_LOST_KHR` map to `NativeStatusCode::SwapchainOutOfDate`;
- `acquireNextImage` and `present` resolve the full `SwapchainState` and reject out-of-range image indices before returning/presenting.

## Alternatives Considered

### Build `PresentCommand` inside `VulkanPresentRuntime`

Rejected. The command is an ECS component and should be produced by a system, not hidden inside a backend runtime.

### Add a default window-visible CTest target

Rejected. Render2D does not own windows or `VkSurfaceKHR`. A default window test would add platform/window policy to a host-owned boundary and would be fragile in headless environments.

## Consequences

- The acquire-to-present ECS stream path is explicit and testable.
- Out-of-date and surface-lost behavior is covered without a real swapchain.
- Acquire/present calls now validate image index against the resolved swapchain state.
- Host integrations still provide the real window-visible proof and RenderDoc capture.
