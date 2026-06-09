# ADR: MemoryCenter and McVector Runtime Memory

Status: Accepted

Date: 2026-06-09

## Context

Render2D previously had a mixed memory story: runtime slot tables used standard dynamic containers, while Vulkan resource and upload runtimes directly called Vulkan memory allocation and mapping functions. The host engine integration requires Render2D-owned memory to be centrally managed by MemoryCenter, while ECS-visible data must remain Strict POD and allocator-free.

## Decision

Render2D runtime-owned dynamic arrays use `Render2D::McVector<T>`, defined in `include/Render2D/Memory/RenderVector.hpp`. This aliases Vector_New `Center::Memory::mc_vector` and routes CPU container allocation through MemoryCenter.

Vulkan buffer/image backing memory is managed through `VulkanMemoryCenterAllocator`, defined in `include/Render2D/Memory/VulkanMemoryCenterAllocator.hpp`. The wrapper builds MemoryCenter Vulkan native providers/adaptors from the active physical device and logical device, allocates and binds buffers/images, exposes persistent mapped pointers for host-visible domains, and centralizes flush/invalidate/deallocate operations.

ECS-visible components keep the existing `id + generation` pattern and do not store MemoryCenter allocation objects, containers, or allocators.

## Alternatives Considered

Continue using `std::vector`: rejected because project-owned container allocation would bypass MemoryCenter and conflict with the merge requirement.

Keep direct `vkAllocateMemory` / `vkMapMemory` in runtimes: rejected because GPU storage ownership would remain fragmented and outside MemoryCenter policy.

Expose MemoryCenter allocation slices to ECS: rejected because ECS components must stay Strict POD resource references, not backend allocation owners.

## Consequences

- Runtime slot tables, test support storage, smoke helpers, and benchmarks no longer use `std::vector` in Render2D source.
- GPU allocation, binding, persistent mapping, flushing, invalidation, and deallocation now go through one wrapper.
- Host-visible writes/readbacks must call MemoryCenter-backed flush/invalidate helpers to support non-coherent memory correctly.
- External dependencies may still use their own internal containers; this ADR only governs Render2D project code.
