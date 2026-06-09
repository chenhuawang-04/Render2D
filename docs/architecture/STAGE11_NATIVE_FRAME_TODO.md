# Stage 11 Native Frame TODO

This checklist tracks the native frame-loop stage after Stage 10 performance/runtime closeout.

## Completed

- [x] 11A: Frame/present/deferred-destroy POD component contracts.
- [x] 11D: Runtime-owned deferred destroy queue with safe frame-lag draining.

## Remaining Route

- [ ] 11B: Vulkan swapchain runtime for host-provided surface/swapchain ownership boundaries.
- [ ] 11C: Acquire/present runtime path using `vkAcquireNextImageKHR` and `vkQueuePresentKHR`.
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

## Verification Commands

```powershell
cmake --build --preset clang-ninja-debug
ctest --test-dir build --output-on-failure
clang-tidy -p build tests\native_deferred_destroy_runtime_test.cpp tests\native_components_test.cpp tests\native_runtime_contract_test.cpp tests\compile_smoke.cpp --quiet
clang-tidy --verify-config --config-file=.clang-tidy
git diff --check
```

Current result: Debug build passed and Debug tests passed 31/31 on 2026-06-09 after 11A/11D implementation.
