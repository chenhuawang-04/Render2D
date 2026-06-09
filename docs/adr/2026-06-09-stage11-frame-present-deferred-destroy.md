# ADR: Stage 11 Frame Present Components and Deferred Destroy Runtime

Status: Accepted

Date: 2026-06-09

## Context

Stage 10 completed command recording, submission, ThreadCenter CPU pipeline work, and native resource/runtime foundations. The next native milestone is a complete frame loop:

```text
beginFrame -> acquire image -> record -> submit -> present -> endFrame
```

Before swapchain acquire/present can be safely integrated, Render2D needs ECS-visible POD records for acquired/present state and a runtime-owned delayed destruction queue. GPU resources must not be destroyed while still referenced by in-flight frames.

## Decision

Add Strict POD native component contracts in:

```text
include/Render2D/Native/NativeComponents.hpp
```

The new records are:

- `SwapchainImageRef`
- `AcquiredImage`
- `PresentCommand`
- `DeferredDestroyCommand`

These records contain IDs, generations, frame indices, flags, and non-owning handle snapshots where useful. They do not own Vulkan lifetimes and do not introduce ECS storage.

Add a runtime-only queue in:

```text
include/Render2D/Native/DeferredDestroyRuntime.hpp
```

`NativeDeferredDestroyRuntime` owns pending destroy commands in `McVector`, validates command shape, supports a configurable safe frame lag, and drains only commands whose retire frame is reached. It does not call Vulkan destroy functions directly; backend runtimes or the host integration layer consume drained commands and perform actual release through existing runtime APIs.

## Alternatives Considered

### Destroy resources immediately on release

Rejected. Immediate release is unsafe once command buffers and queue submission are in flight.

### Store ThreadCenter or Vulkan lifecycle objects in ECS components

Rejected. ECS components remain Strict POD and carry only IDs, generations, frame indices, flags, and handle snapshots.

### Put deferred destroy ownership inside every Vulkan runtime

Rejected for this stage. A single queue keeps frame-lifetime policy centralized while preserving existing resource/runtime release APIs.

## Consequences

- Stage 11 has a safe foundation for swapchain resize, present, and in-flight resource retirement.
- Host ECS can migrate these records directly because they are Strict POD.
- Runtime arrays continue using `McVector`.
- Actual Vulkan destruction remains centralized in backend runtime code and is not performed from ECS systems.
- Future acquire/present work can consume `AcquiredImage` and `PresentCommand` without changing the component contract.
