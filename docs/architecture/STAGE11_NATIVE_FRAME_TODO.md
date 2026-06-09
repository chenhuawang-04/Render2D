# Stage 11 Native Frame TODO

This checklist tracks the native frame-loop stage after Stage 10 performance/runtime closeout.

## Completed

- [x] 11A: Frame/present/deferred-destroy POD component contracts.
- [x] 11B: Vulkan swapchain runtime for host-provided surface/swapchain boundaries.
- [x] 11C: Acquire/present runtime path using `vkAcquireNextImageKHR` and `vkQueuePresentKHR`.
- [x] 11D: Runtime-owned deferred destroy queue with safe frame-lag draining.

## Remaining Route

- [ ] 11E: State-level and optional Vulkan smoke coverage for resize/out-of-date/present flow.
- [ ] 11F: Stage 11 closeout docs, final verification, and merge guidance.

## Non-negotiable Constraints

- Components remain Strict POD.
- Host ECS owns component streams; Render2D does not introduce production ECS storage.
- Runtime-owned dynamic arrays use `McVector` / MemoryCenter.
- GPU memory remains MemoryCenter-managed.
- ThreadCenter types do not enter ECS components.
- Vulkan handles in components are non-owning snapshots only.

## 11A / 11D Current Result

Added components:

```text
SwapchainImageRef
AcquiredImage
PresentCommand
DeferredDestroyCommand
```

Added runtime:

```text
NativeDeferredDestroyRuntime
```

`NativeDeferredDestroyRuntime` queues `DeferredDestroyCommand` records, applies an optional safe frame lag, and drains commands only after their retire frame has been reached. It preserves pending order, rejects invalid commands, reports insufficient output capacity without mutating the queue, and supports U32 frame-index wrap comparison.

## 11B Current Result

Added runtime:

```text
VulkanSwapchainRuntime
```

`VulkanSwapchainRuntime` initializes from `VkDevice`, creates swapchains from host-provided `VkSurfaceKHR`, can adopt host-provided `VkSwapchainKHR`, queries swapchain images, creates image views, and exposes `SwapchainState` / `SwapchainImageRef` POD records. It validates stale generations for state/image/native-handle resolution and releases image views plus owned swapchain handles on release/shutdown.

Boundary:

- Render2D does not create windows.
- Render2D does not create `VkSurfaceKHR`.
- Swapchain image memory is owned by Vulkan swapchain internals, not by Render2D GPU allocation.
- Runtime-owned arrays use `McVector`.

## 11C Current Result

Added runtime:

```text
VulkanPresentRuntime
```

`VulkanPresentRuntime` initializes from `VkDevice` and present `VkQueue`, resolves swapchain/sync state through `VulkanSwapchainRuntime` and `VulkanSyncRuntime`, calls `vkAcquireNextImageKHR`, emits `AcquiredImage`, and presents `PresentCommand` through `vkQueuePresentKHR`.

`AcquiredImage` and `PresentCommand` now include sync generation fields so stale `FrameSync` records can be rejected by the sync runtime.

Automated coverage remains headless and state-level: invalid initialization, duplicate initialization, stale swapchain acquire/present, invalid present command, and unsupported-domain paths. Real window-visible present smoke remains 11E.

## Verification Commands

```powershell
cmake --build --preset clang-ninja-debug
ctest --test-dir build --output-on-failure
clang-tidy -p build tests\native_deferred_destroy_runtime_test.cpp tests\native_components_test.cpp tests\native_runtime_contract_test.cpp tests\compile_smoke.cpp --quiet
clang-tidy -p build tests\vulkan_swapchain_runtime_test.cpp tests\native_runtime_contract_test.cpp --quiet
clang-tidy -p build tests\vulkan_present_runtime_test.cpp tests\native_components_test.cpp tests\native_runtime_contract_test.cpp --quiet
clang-tidy --verify-config --config-file=.clang-tidy
git diff --check
```

Current result: Debug tests passed 33/33 and Perf tests passed 42/42 on 2026-06-09 after 11C implementation.
