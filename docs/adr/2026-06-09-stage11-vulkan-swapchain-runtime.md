# ADR: Stage 11 Vulkan Swapchain Runtime

Status: Accepted

Date: 2026-06-09

## Context

Render2D already owns Vulkan resources, command buffers, sync, queue submission, and a deferred-destroy queue. The remaining native frame-loop work needs a swapchain boundary that supports host-engine window ownership while keeping ECS components as Strict POD records.

The host engine owns the window and normally creates the `VkSurfaceKHR`. Render2D may create a `VkSwapchainKHR` from that surface or adopt an existing swapchain depending on integration policy.

## Decision

Add:

```text
include/Render2D/Native/VulkanSwapchainRuntime.hpp
```

`VulkanSwapchainRuntime`:

- initializes from `VkDevice`;
- creates a swapchain from a host-provided `VkSurfaceKHR`;
- can adopt a host-provided `VkSwapchainKHR`;
- queries swapchain images;
- creates and owns image views for those images;
- exposes `SwapchainState` and `SwapchainImageRef` POD records;
- validates stale generations when resolving state, images, and native handles;
- releases image views and owned swapchain handles on shutdown/release.

The runtime does not create windows, does not create surfaces, and does not allocate swapchain image memory. Swapchain image memory is owned by Vulkan swapchain internals; Render2D only owns image views and optional swapchain handles.

## Alternatives Considered

### Require Render2D to own the window and surface

Rejected. The user already has an engine, so window/surface ownership must remain externally controlled.

### Only support externally created swapchains

Rejected. Creation from a host-provided surface is useful for Render2D-managed renderer setups and keeps future acquire/present code testable behind one runtime contract.

### Store Vulkan swapchain ownership directly in ECS components

Rejected. ECS records remain Strict POD IDs/generations. Runtime slot metadata owns backend lifetimes.

## Consequences

- Stage 11 now has the swapchain/image-view runtime needed before acquire/present.
- Host integrations can choose create or adopt mode.
- Swapchain images are represented as POD refs and resolved through runtime generation checks.
- Future 11C acquire/present work can build on `SwapchainState`, `SwapchainImageRef`, `AcquiredImage`, `PresentCommand`, and `FrameSync`.
