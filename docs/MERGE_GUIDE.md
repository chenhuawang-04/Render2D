# Render2D Merge Guide

How to merge Render2D into a host engine as its 2D rendering backend. This is the Stage 23 deliverable: it absorbs the 23B (runtime resolve-API surface), 23C (39-item reconciliation), and 23F (ordered integration walkthrough) work into one reference.

Companion documents: `docs/ProjectMergeTODO.md` (the 39 numbered merge constraints, reconciled in §4 below), `docs/architecture/NATIVE_RUNTIME_CONTRACT.md` (per-runtime ownership detail), `docs/ARCHITECTURE.md` (pipeline + runtime inventory), and `ReinforcementPlan.md` §1 (the non-negotiable red lines).

## 0. Scope and honesty

Render2D is a 2D **rendering backend**, not an application or engine. Windowing/app loop, input, audio, the production ECS/scene graph, asset pipeline, animation, physics, gameplay, and editor tooling are the host engine's job and are intentionally absent here (see `docs/ARCHITECTURE.md` → *Scope and non-goals*).

The real merge runs in the **host repository** — there is no host engine in this repo. What this repo provides is proof that Render2D is mergeable, plus the artifacts a host engineer needs. Two in-repo proofs underwrite this guide:

- **23A — storage independence.** `tests/support/HostLikeEcs.hpp` is a host-shaped SoA archetype ECS (deliberately unlike the by-type `ComponentStreamStorage` test ECS). `render2d.host_like_ecs_adapter` runs the same production chain (`SpatialCull → CommandBuild → SpriteInstanceBuild → Batch`) from both storages and asserts every derived stream is **byte-identical**. Because the systems only ever see `std::span`, a brand-new storage type compiling against them *is* the guarantee that the boundary is span-only (ProjectMergeTODO #1).
- **23D — host data to a real frame.** `tests/host_present_frame_smoke.cpp` (`render2d.host_present_frame`) drives `HostLikeEcs` data through the span-only chain into a real `VulkanSpriteRenderEncoder` draw, onto an acquired swapchain image, and **presents** it — then asserts the swapchain readback equals the offscreen baseline byte-for-byte. This is the first real sprite draw in the repo to reach a swapchain (ProjectMergeTODO #9 for the sprite path).

Neither proof changes a runtime contract. Both are test-only; the present-host they use is the optional, isolated Stage 22 subsystem that a host turns **off** at merge (`RENDER2D_BUILD_PRESENT_HOST=OFF`) to supply its own surface.

The merge must preserve every red line in `ReinforcementPlan.md` §1 (Strict POD components; host ECS owns component streams; `Render2D::McVector` for Render2D-owned storage; Vulkan memory only through `VulkanMemoryCenterAllocator`; fast_math-only math; `id + generation` refs; ThreadCenter runtime-only; U32-bounded streams; window/surface owned by the host).

## 1. Ownership split

| Owned by the **host engine** | Owned by **Render2D native runtime** |
|---|---|
| `VkInstance`, `VkPhysicalDevice`, `VkDevice`, `VkQueue` | Backend slot tables (resource / pipeline / descriptor / command / sync / swapchain image views / sampler / atlas / material graph) |
| Window and `VkSurfaceKHR` | GPU buffer/image memory, via `VulkanMemoryCenterAllocator` only |
| Production ECS storage + all POD component streams | Upload ring segments (`VulkanUploadRingRuntime`) |
| Frame scheduling, on-screen present, window-visible capture | Deferred-destroy queue (`NativeDeferredDestroyRuntime`) |
| Asset/atlas source data, font files, material graph policy | `id + generation` validation and stale-reference rejection |

The host hands Render2D systems `std::span` over its component streams and calls Render2D runtimes to resolve `id + generation` refs into Vulkan objects (§3). Render2D never holds a `VkInstance`/`VkDevice`/`VkSurfaceKHR` lifetime and never owns ECS storage.

## 2. Ordered integration walkthrough

A single, ordered sequence. Each step names the in-repo template that demonstrates it.

1. **Turn off the present-host.** Configure with `-DRENDER2D_BUILD_PRESENT_HOST=OFF`. SDL3 and the `render2d_present_host_support` target drop out of the build entirely (verified: whole-tree builds with zero SDL/RenderDoc references and the present-host tests unregister). The host supplies its own `VkInstance`/`VkDevice`, window, and `VkSurfaceKHR`.
2. **Replace the test ECS with the host ECS.** Delete the test-only scaffolding (`tests/support/TemporaryEcsStorage.hpp`, `tests/support/ComponentStreamView.hpp`) and feed components from the host ECS. Shape the boundary after `tests/support/HostLikeEcs.hpp` (23A) — its SoA columns map directly onto a host archetype store, and `render2d.host_like_ecs_adapter` is the byte-identical proof that the swap is safe. Systems take `std::span<const T>` in and `std::span<T>` out; they never see the storage type.
3. **Own the POD component streams in the host ECS.** Render2D systems consume and produce only Strict POD components — e.g. `Transform`, `LocalBounds`, `VisibilityMask`, `Sprite`, `WorldTransform`, `VisibleItem`, `DrawCommand`, `BatchCommand`, `SpriteVertex`, `SpriteInstance`, `SpriteDrawPacket`, the text components, the present records (`SwapchainImageRef`, `AcquiredImage`, `PresentCommand`), and the POD refs (`BufferRef`, `ImageRef`, `PipelineRef`, `DescriptorSlice`, `SamplerRef`, `NativeCommandBufferRef`, `FrameSync`, `UploadRingSlice`, `SwapchainState`). Keep Render2D's runtime slot tables, GPU memory, and deferred-destroy queue inside Render2D.
4. **Wire the runtime resolve APIs (§3).** When the host needs the Vulkan object behind a ref, it calls the owning runtime's resolve method; the runtime validates the generation and rejects stale refs. Map the host's math/streams onto fast_math POD types at a single conversion edge (ProjectMergeTODO #14).
5. **Drive the frame loop.** `acquireNextImage` → run the CPU chain over host spans → build/upload `SpriteInstance[]` → encode via `VulkanSpriteRenderEncoder` → submit → `present`; handle `SwapchainOutOfDate` by recreating the swapchain. `tests/host_present_frame_smoke.cpp` (23D) is the end-to-end template; the production sprite shader/projection (camera → clip) is the host's vertex-shader concern — Render2D's CPU chain stops at world-space affine.
6. **Run the host-side gate.** Validation-layer-clean present, plus the host's own window-visible capture (Render2D's offscreen readback remains the correctness reference; on-screen is validated against it, exactly as 22D/23D do in-repo).

> **Consumption model.** Steps 2–5 describe **source reuse** — the host adds Render2D to its build and reuses the targets (the primary merge path). Render2D can alternatively be consumed as an **installed package**: `cmake --install <build> --component Render2D` then `find_package(Render2D)` (Stage 25; `render2d.packaging_consumer` / `render2d.packaging_installed` exercise both paths). The installed `Render2DConfig.cmake` re-runs the same three-tier dependency resolver, so a host that already defines the engine-dep targets reuses them untouched — no vendored private headers. See `docs/adr/2026-06-18-stage25-consumability-packaging.md`.

## 3. Runtime resolve-API surface

Every ECS-visible `id + generation` ref resolves through the runtime that owns the backing Vulkan object. Each runtime exposes a POD-ref validation variant (`resolve<Ref>` → re-validated ref) and, where applicable, a native-handle variant (`resolveNative<Object>` → `VkObject`). All return `NativeResult` and reject stale generations with `NativeStatusCode::StaleReference`.

| Ref (ECS-visible POD) | Owning runtime | Native-handle resolve | Backing Vulkan object |
|---|---|---|---|
| `BufferRef` | `VulkanResourceRuntime` | `resolveNativeBuffer` | `VkBuffer` (+ MemoryCenter slice) |
| `ImageRef` | `VulkanResourceRuntime` | `resolveNativeImage` | `VkImage` + `VkImageView` (+ slice) |
| `PipelineRef` | `VulkanPipelineRuntime` | `resolveNativePipeline` | `VkPipeline` (+ layout) |
| `DescriptorSlice` | `VulkanDescriptorRuntime` | `resolveNativeDescriptorSet` | `VkDescriptorSet` |
| `NativeCommandBufferRef` | `VulkanCommandRuntime` / `VulkanThreadCommandRuntime` | `resolveNativeCommandBuffer` | `VkCommandBuffer` |
| `FrameSync` | `VulkanSyncRuntime` | `resolveNativeSync` | `VkSemaphore` / `VkFence` |
| `SwapchainState` | `VulkanSwapchainRuntime` | `resolveNativeSwapchain` | `VkSwapchainKHR` |
| `SwapchainImageRef` | `VulkanSwapchainRuntime` | `resolveNativeSwapchainImage` | swapchain `VkImage` + view |
| `UploadRingSlice` | `VulkanUploadRingRuntime` | `resolveNativeBuffer` | upload `VkBuffer` + offset |
| `SamplerRef` | `VulkanSamplerRuntime` | `resolveNativeSampler` | `VkSampler` |
| `VulkanAtlasImageRef` | `VulkanTextureAtlasRuntime` | `resolveImageRef` → backing `ImageRef` | atlas `VkImage` (via `VulkanResourceRuntime`) |

Material identity resolves through the CPU-only `VulkanSpriteMaterialGraph` (`resolveBinding` / `resolvePipeline` / `resolveSamplerIndex` / `resolveParams`), which holds only `id + generation` refs, no Vulkan handles. The bindless path (`VulkanBindlessTextureTable`) binds one descriptor set per frame and indexes textures by id; the non-bindless combined-image-sampler path stays the correctness/compatibility fallback (Stage 20). Skeleton CPU-only variants (`ResourceRuntime`, `PipelineRuntime`, `DescriptorRuntime`, `SwapchainRuntime`, `CommandRuntime`, `DeviceRuntime`) expose the same `resolve<Ref>` validation without Vulkan, for host code paths that run windowless.

Device/queue identity (`DeviceHandle`, `QueueHandle`) is resolvable via the device-runtime skeleton, but at merge the **host owns the `VkDevice`/`VkQueue`** — Render2D consumes them, it does not create them.

## 4. 39-item merge reconciliation

Status legend: **Done** = contract implemented and proven in-repo; **Host** = the host engine owns this at merge; **Marker** = a stage-closed/historical note, not an active constraint. Items with both tags carry the contract in-repo while leaving a named responsibility to the host.

| # | Item | Status | Satisfied by / merge action |
|---|---|---|---|
| 1 | ECS framework exists only for tests | Host | Systems are span-only; 23A proves byte-identical across storages. Action: replace `tests/support/` ECS with host ECS. |
| 2 | Resource refs use id + generation | Done | All `*Ref`/state records (Stage 7+). |
| 3 | Native runtime is not ECS storage | Done | Runtime slot tables stay in Render2D. |
| 4 | Pre-8B native runtime CPU-only | Marker | Superseded — Vulkan handles attached Stage 8B+. |
| 5 | Stage 8A encode/submit contract | Done | `EncodeSystem`/`SubmitSystem` POD descriptors. |
| 6 | Stage 8B owns Vulkan command objects | Done | `VulkanCommandRuntime` owns pool/buffers. |
| 7 | Stage 8C-G Vulkan runtimes own handles | Done | Host stores only the listed POD refs. |
| 8 | Upload ring slots not reused early | Done | `beginFrame → … → completeFrame`; host obeys fence order. |
| 9 | Visible proof was offscreen | Done + Host | Closed in-repo by Stage 22 + 23D; host runs its own on-screen capture. |
| 10 | MemoryCenter/McVector mandatory | Done | Enforced by `scripts/scan_constraints.sh`. Host keeps Render2D-owned storage on McVector. |
| 11 | Text/Glyph POD-only (9A) | Done + Host | Components POD; host owns UTF-8 backing buffers outside ECS. |
| 12 | FreeType vendored but dormant | Host | Superseded by Stage 19 (active). Action: host picks FreeType license policy before shipping. |
| 13 | Stage 9 text pipeline dependency-free | Done | Pure systems (`TextShapingSystem`) stay dependency-free. |
| 14 | Render math is fast_math POD-only | Done + Host | `Vec2/Mat3/Aabb2` = `MMath`; host maps its math at one edge. |
| 15 | ThreadCenter runtime-only | Done | Not in components/umbrella; threaded support targets only. |
| 16 | `TransformDirtyItem` ECS-visible | Done | Host fills dirty streams. |
| 17 | Draw sort key is the batch/sort contract | Done + Host | `makeDrawSortKey`; host generates the same key layout. |
| 18 | Upload/descriptor compaction outputs components | Done | `UploadCoalesceSystem`/`DescriptorCompactionSystem`. |
| 19 | Per-thread command pools runtime-only | Done | `VulkanThreadCommandRuntime`; thread index not in component contract. |
| 20 | Stage 10 closed | Marker | Historical. |
| 21 | Stream sizes U32-bounded | Done + Host | Systems reject non-U32; host splits larger datasets. |
| 22 | Threaded CPU pipeline threshold-gated | Done + Host | `ParallelPolicy.hpp` (21E); host tunes the threshold per platform. |
| 23 | Deferred destroy is a runtime queue | Done + Host | `NativeDeferredDestroyRuntime`; host drains after retire frame/fence. |
| 24 | Frame/present components are POD state | Done + Host | Records POD; **host owns window/surface**. |
| 25 | Swapchain boundary host-surface-first | Done + Host | `VulkanSwapchainRuntime` create/adopt; host owns surface. |
| 26 | Acquire/present uses sync id + generation | Done | `VulkanPresentRuntime` + `FrameSync` generations. |
| 27 | Acquire-to-present is a component transform | Done + Host | `PresentCommandBuildSystem`; host runs visible capture. |
| 28 | Stage 11 native frame boundary complete | Done + Host | Integrate as backend; window/surface stays host-side. |
| 29 | Sprite GPU instance data is ECS component | Done | `SpriteInstanceBuildSystem`; 23D drives it from host-shaped data. |
| 30 | Sprite instance upload = typed command + runtime copy | Done | `SpriteInstanceUploadSystem` + `VulkanSpriteInstanceUploadRuntime`. |
| 31 | Sprite pipeline layout is runtime contract | Done | `VulkanSpritePipelineRuntime`; host owns vertex/instance streams. |
| 32 | Sprite render encoder is runtime-only | Done + Host | `VulkanSpriteRenderEncoder`; smoke shader = instance color only (23D uses an empty descriptor span). Host supplies production texture/material descriptor policy. |
| 33 | Stage 12 sprite GPU path closed | Marker | Historical. |
| 34 | Sampler refs ECS components; `VkSampler` runtime-only | Done | `SamplerRef` + `VulkanSamplerRuntime`. |
| 35 | Textured sprite sampling proven; atlas/material host-facing | Done + Host | Sampled path proven; host owns production atlas/material policy. |
| 36 | Texture/material generations flow through batches | Done | Generations preserved through draw/batch/instance/packet. |
| 37 | `SpriteDrawPacket` is the draw binding record | Done | `SpriteDrawPacketBuildSystem` + `recordPackets`. |
| 38 | Atlas regions are ECS components; atlas images runtime/host | Done + Host | `TextureAtlasBuildSystem` + `VulkanTextureAtlasRuntime`; host owns production atlas images. |
| 39 | Atlas region UVs GPU-proven; production atlas external | Done + Host | UV propagation proven (Stage 16); host supplies production atlas runtime. |

There are no **un**-addressed items: every constraint is either implemented and proven in-repo, or is an explicit host responsibility with the contract already in place, or is a historical marker.

## 5. Removal targets at merge

These exist only to validate Render2D in isolation and should be removed or disabled when the host ECS and surface take over:

- `tests/support/TemporaryEcsStorage.hpp`, `tests/support/ComponentStreamView.hpp` — the test ECS (ProjectMergeTODO #1). Replace with the host ECS; `tests/support/HostLikeEcs.hpp` is the shape template, not production code.
- The present-host subsystem — set `RENDER2D_BUILD_PRESENT_HOST=OFF`; `third_party/sdl`, `render2d_present_host_support`, `include/Render2D/Present/*`, and the `render2d.present_*` / `render2d.host_present_frame` tests all drop out. The host supplies its own surface and capture automation.
- The `tests/` and `bench/` executables themselves — they are the in-repo verification harness, not a shipping artifact.

## 6. Verification you can re-run in-repo

The Stage 23E gate, which a host can re-run to confirm mergeability before integrating:

```bash
cmake --preset clang-ninja-debug && cmake --build --preset clang-ninja-debug && ctest --preset clang-ninja-debug
cmake --preset clang-ninja-perf  && cmake --build --preset clang-ninja-perf  && ctest --preset clang-ninja-perf
# present-host OFF whole-tree (the merge-time configuration):
cmake -B build_off -G Ninja -DRENDER2D_BUILD_PRESENT_HOST=OFF <engine-deps flags> && cmake --build build_off && ctest --test-dir build_off
bash scripts/scan_constraints.sh   # no std::vector / no direct Vulkan memory API / no Render2D-local math
git diff --check
```

The OFF whole-tree build is the one that matters most for merge confidence: it proves Render2D's core compiles, links, and passes with **no** SDL/RenderDoc/present-host references — i.e. windowless, exactly as a host integrates it.
