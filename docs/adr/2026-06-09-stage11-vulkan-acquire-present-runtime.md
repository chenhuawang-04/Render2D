# ADR: Stage 11 Vulkan Acquire and Present Runtime

Status: Accepted

Date: 2026-06-09

## Context

Stage 11A/11D added frame/present POD records and deferred destruction. Stage 11B added `VulkanSwapchainRuntime` for host-provided surfaces and swapchain image refs. The next step is to connect swapchain state and frame sync to Vulkan acquire/present calls without moving backend lifetimes into ECS components.

## Decision

Add:

```text
include/Render2D/Native/VulkanPresentRuntime.hpp
```

`VulkanPresentRuntime`:

- initializes from `VkDevice` and present `VkQueue`;
- calls `vkAcquireNextImageKHR` using `SwapchainState` and `FrameSync`;
- emits `AcquiredImage`;
- calls `vkQueuePresentKHR` using `PresentCommand`;
- resolves swapchain and sync objects through `VulkanSwapchainRuntime` and `VulkanSyncRuntime`;
- maps Vulkan errors to `NativeStatusCode`.

`AcquiredImage` and `PresentCommand` now include sync generations so stale `FrameSync` records can be rejected by the sync runtime.

## Alternatives Considered

### Put acquire/present directly inside `VulkanSwapchainRuntime`

Rejected. Swapchain ownership and queue-present execution are separate concerns. Keeping present as its own runtime makes queue ownership explicit.

### Store `VkSemaphore` directly in `PresentCommand`

Rejected. ECS components must keep id + generation, not Vulkan lifetime handles.

### Require live window/surface tests in CTest

Rejected for this stage. The current automated suite remains headless; state-level tests verify invalid config, stale reference, and unsupported-domain paths. Window-visible smoke remains a later Stage 11 item.

## Consequences

- Acquire/present API shape is now available for host integration.
- `FrameSync` generation checks remain centralized in `VulkanSyncRuntime`.
- The window-visible path still requires a host-created surface and real swapchain smoke.
- Future frame-loop orchestration can compose frame runtime, sync runtime, swapchain runtime, present runtime, submit runtime, and deferred destroy runtime.
