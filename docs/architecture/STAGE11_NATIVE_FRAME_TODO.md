# Stage 11 Native Frame TODO

This checklist tracks the native frame-loop stage after Stage 10 performance/runtime closeout.

## Completed

- [x] 11A: Frame/present/deferred-destroy POD component contracts.
- [x] 11B: Vulkan swapchain runtime for host-provided surface/swapchain boundaries.
- [x] 11C: Acquire/present runtime path using `vkAcquireNextImageKHR` and `vkQueuePresentKHR`.
- [x] 11D: Runtime-owned deferred destroy queue with safe frame-lag draining.
- [x] 11E: State-level acquire/present flow and out-of-date coverage.
- [x] 11F: Stage 11 closeout docs, final verification, and merge guidance.

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

Automated coverage remains headless and state-level: invalid initialization, duplicate initialization, stale swapchain acquire/present, invalid present command, and unsupported-domain paths.

## 11E Current Result

Added system:

```text
PresentCommandBuildSystem
```

`PresentCommandBuildSystem` converts `AcquiredImage[]` to `PresentCommand[]` as a pure ECS component-stream transform. It preserves swapchain id, image index, frame index, swapchain generation, sync id, sync generation, and flags. It rejects zero swapchain/sync generations and never owns production ECS storage.

`VulkanPresentRuntime` now exposes `mapVulkanAcquirePresentResult` for state-level testing of acquire/present result classification. `acquireNextImage` and `present` resolve the full `SwapchainState` and reject out-of-range image indices before returning/presenting.

Automated smoke remains host-window-free. A real window-visible capture belongs to host integration because Render2D does not own windows or surfaces.

## 11F Closeout Result

Stage 11 is complete. The final contract is:

```text
FrameSync / SwapchainState / SwapchainImageRef
    -> VulkanSwapchainRuntime + VulkanPresentRuntime
    -> AcquiredImage[]
    -> PresentCommandBuildSystem
    -> PresentCommand[]
    -> VulkanPresentRuntime::present
    -> NativeDeferredDestroyRuntime for frame-safe retirement
```

Merge guidance:

- the host engine owns window creation and `VkSurfaceKHR`;
- host ECS owns `SwapchainState[]`, `SwapchainImageRef[]`, `AcquiredImage[]`, `PresentCommand[]`, and `DeferredDestroyCommand[]`;
- Render2D owns only runtime slots and backend objects behind id + generation references;
- swapchain image memory is Vulkan-owned internally, not MemoryCenter allocated;
- real window-visible capture should be run by the host integration layer.

## Verification Commands

```powershell
cmake --build --preset clang-ninja-debug
ctest --test-dir build --output-on-failure
clang-tidy -p build tests\native_deferred_destroy_runtime_test.cpp tests\native_components_test.cpp tests\native_runtime_contract_test.cpp tests\compile_smoke.cpp --quiet
clang-tidy -p build tests\vulkan_swapchain_runtime_test.cpp tests\native_runtime_contract_test.cpp --quiet
clang-tidy -p build tests\vulkan_present_runtime_test.cpp tests\native_components_test.cpp tests\native_runtime_contract_test.cpp --quiet
clang-tidy -p build tests\present_command_system_test.cpp tests\vulkan_present_runtime_test.cpp --quiet
clang-tidy --verify-config --config-file=.clang-tidy
git diff --check
```

Current result: Debug tests passed 34/34 and Perf tests passed 43/43 on 2026-06-09 after 11F closeout. `clang-tidy`, `clang-tidy --verify-config`, `git diff --check`, `std::vector`, Vulkan memory API, and old-math scans passed.
