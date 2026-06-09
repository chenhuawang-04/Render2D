# ADR: Stage 12 Sprite Instance Upload Path

Status: Accepted

Date: 2026-06-09

## Context

Stage 12A/12B defined the ECS-visible sprite instance stream. The next step needs to move `SpriteInstance[]` into GPU memory without adding production ECS storage to Render2D and without bypassing MemoryCenter-backed Vulkan allocation/mapping.

## Decision

Add `SpriteInstanceUploadCommand` in:

```text
include/Render2D/Component/Sprite.hpp
```

It is a Strict POD component carrying:

- source instance range;
- destination buffer id + generation;
- destination byte offset;
- frame index and flags.

Add `SpriteInstanceUploadSystem` in:

```text
include/Render2D/System/SpriteInstanceSystem.hpp
```

It converts typed sprite upload commands plus `SpriteInstance[]` into generic `UploadCommand[]` records with `kUploadKindSpriteInstance`. The system only validates ranges and computes byte offsets/counts.

Add `VulkanSpriteInstanceUploadRuntime` in:

```text
include/Render2D/Native/VulkanSpriteInstanceUploadRuntime.hpp
```

The runtime allocates and writes a `VulkanUploadRingRuntime` slice, resolves the upload-ring native buffer, and records a copy into a `VulkanResourceRuntime` managed destination buffer. `VulkanResourceRuntime` now exposes offset-aware buffer copy helpers and a native-source-buffer to managed-buffer copy helper.

## Alternatives Considered

### Upload directly from `SpriteInstanceUploadSystem`

Rejected. Systems must remain component-to-component transforms and must not call Vulkan or own backend lifetimes.

### Store raw `VkBuffer` in the upload command

Rejected. ECS-visible resource references must use id + generation. Raw Vulkan handles are runtime resolution details and are not lifetime authority.

### Use `UploadCommand` only

Rejected for the typed source path because existing `UploadCommand` does not carry destination generation. The typed sprite command preserves generation validation before the runtime copy.

## Consequences

- Host ECS can own both `SpriteInstanceUploadCommand[]` and `UploadCommand[]`.
- Sprite instance GPU upload now uses MemoryCenter-backed upload-ring and resource runtimes.
- No direct `VkDeviceMemory`, Vulkan allocation, or Vulkan mapping is introduced.
- Sprite descriptors, pipeline layout, shaders, and real offscreen sprite draw remain Stage 12D/12E work.
