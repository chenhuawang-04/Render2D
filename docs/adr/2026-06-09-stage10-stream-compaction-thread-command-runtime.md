# ADR: Stage 10 Stream Compaction and Thread Command Runtime

Status: Accepted

Date: 2026-06-09

## Context

Stage 10I/10J complete the remaining performance/runtime work for the current phase:

- compact upload and descriptor component streams before native encoding;
- assign Vulkan command buffers to per-thread command pools for parallel recording ownership;
- preserve the rule that ECS sees only Strict POD component records.

The host engine will eventually own ECS storage, so Render2D must not introduce production ECS managers or backend handles in components.

## Decision

Add dependency-free CPU systems in:

```text
include/Render2D/System/UploadDescriptorCompactionSystem.hpp
```

`UploadCoalesceSystem` merges adjacent `UploadCommand[]` records when resource, kind, flags, source offsets, and destination offsets are contiguous. `DescriptorCompactionSystem` merges adjacent `DescriptorSlice[]` ranges when descriptor set id, generation, and table ranges are contiguous. Both systems use caller-owned spans, allocate nothing, and return `SystemResult`.

Add a separate Vulkan runtime in:

```text
include/Render2D/Native/VulkanThreadCommandRuntime.hpp
```

`VulkanThreadCommandRuntime` creates one `VkCommandPool` per runtime thread slot, allocates command buffers from the selected pool, keeps `NativeCommandBufferRef` id/generation validation, and stores thread ownership only in runtime slot metadata.

## Alternatives Considered

### Put compaction inside Vulkan runtimes

Rejected. Upload and descriptor compaction transforms ECS component streams and should remain testable without Vulkan.

### Extend `NativeCommandBufferRef` with a thread id

Rejected. Thread ownership is runtime scheduling metadata, not an ECS-visible component requirement.

### Replace `VulkanCommandRuntime`

Rejected. The existing single-pool runtime remains a simpler reference path. The thread-pool runtime is added beside it to avoid destabilizing current tests and consumers.

## Consequences

- Stage 10 now has stream compaction coverage and a per-thread Vulkan command ownership runtime.
- ECS component contracts remain unchanged and Strict POD.
- Render2D-owned runtime arrays still use `McVector`.
- Vulkan command pools and command buffers remain runtime-only; ECS stores only `NativeCommandBufferRef`.
- A dedicated benchmark target records upload/descriptor compaction cost and output reduction.
