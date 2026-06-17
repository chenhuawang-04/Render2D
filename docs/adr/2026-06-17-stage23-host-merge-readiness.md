# ADR: Host-engine merge readiness and the host-ECS boundary (Stage 23)

Status: Accepted

Date: 2026-06-17

## Context

Render2D is a header-only 2D rendering backend designed to be merged into a host
engine. Stages 0–22 built the full CPU pipeline, the Vulkan runtimes, the sprite/
text/atlas/bindless GPU paths, and (Stage 22) an optional, isolated on-screen
present-host. The 39 numbered constraints in `docs/ProjectMergeTODO.md` accumulated
across those stages as the merge contract.

Stage 23 is the convergence point. The honest constraint is that **there is no host
engine in this repository** — the real merge runs in the host repo. So Stage 23
cannot *perform* the merge; it can only **prove Render2D is mergeable** and **ship
the artifacts** a host engineer needs. Two questions had to be answered concretely
rather than asserted:

1. Are the systems genuinely storage-agnostic, or have they quietly grown a
   dependency on the test ECS (`tests/support/ComponentStreamStorage`)?
2. Can host-shaped data actually reach a real, presented frame through the
   unchanged runtimes?

## Decision

Stage 23 = **merge-readiness convergence**, delivered as proofs + documentation,
with two deliberate honesty corrections recorded below.

**The host-ECS boundary is `std::span`, and it is proven, not asserted.** Render2D
systems consume `std::span<const T>` and write `std::span<T>`; they never name a
storage type. To prove the boundary actually holds, 23A adds
`tests/support/HostLikeEcs.hpp` — a host-shaped SoA archetype store plus a frame
arena, *intentionally unlike* the by-type test ECS — and `render2d.host_like_ecs_adapter`
runs the same chain (`SpatialCull → CommandBuild → SpriteInstanceBuild → Batch`)
from both storages, asserting every derived stream is byte-identical. The strongest
guarantee is at compile time: a brand-new storage type compiling against the
systems *is* the proof that the only coupling is the span.

**Host-shaped data reaches a real frame.** 23D (`tests/host_present_frame_smoke.cpp`)
drives `HostLikeEcs` data through the span-only chain into a real
`VulkanSpriteRenderEncoder` draw, onto an acquired swapchain image, and presents it,
then asserts the swapchain readback equals the offscreen baseline byte-for-byte —
the first real sprite draw in the repo to reach a swapchain. It changes no runtime
contract: the sprite content reaches the swapchain via the same raw
`vkCmdCopyImage`-on-resolved-handles idiom Stage 22D uses, and the present-host
creates no `VkInstance`/`VkDevice` (window/surface stay the host's).

**The merge artifact is `docs/MERGE_GUIDE.md`.** It records the ownership split
(host owns instance/device/queue/window/surface and the production ECS; Render2D
owns slot tables, GPU memory, upload ring, deferred-destroy), an ordered
integration walkthrough, the runtime resolve-API surface (each `id + generation`
ref → its owning runtime's `resolve*` method), and a per-item reconciliation of all
39 `ProjectMergeTODO` constraints (every item is implemented-in-repo, an explicit
host responsibility with the contract already in place, or a historical marker —
none unaddressed).

**No runtime or component contract changes in Stage 23.** Every red line in
`ReinforcementPlan.md` §1 holds: Strict POD components, host ECS owns streams,
`McVector` for Render2D-owned storage, `VulkanMemoryCenterAllocator` for Vulkan
memory, fast_math-only math, `id + generation` refs, ThreadCenter/present-host out
of the umbrella, U32-bounded streams, host-owned window/surface.

### Honest corrections to the original Stage 23 sketch

1. **23F cannot *delete* the test ECS in this repo.** Render2D self-tests through it
   and has no host ECS to replace it. So this repo documents the test ECS as a
   *merge-time removal target* and uses the 23A adapter as proof of replaceability,
   rather than removing it.
2. **23B real wiring is host-repo work.** The native-runtime resolve contract is
   complete and documented here, but actually wiring a host's device/queue/surface
   and replacing the ECS is performed in the host repository. This repo proves it is
   *feasible and documented*, not *executed*.

## Consequences

- A host engineer has one ordered guide (`MERGE_GUIDE.md`), backed by two runnable
  proofs, to integrate Render2D as a windowless rendering backend.
- The span-only boundary is regression-protected: if a system ever grows a
  dependency on a concrete storage type, `render2d.host_like_ecs_adapter` stops
  compiling.
- The present-host's `RENDER2D_BUILD_PRESENT_HOST=OFF` whole-tree build is the
  canonical merge configuration and is part of the Stage 23E gate; a green OFF build
  with zero SDL/RenderDoc references is the strongest single mergeability signal.
- Render2D's final form is fixed: it is the host's 2D rendering backend, not an
  application. Windowing, scene ECS, asset loading, and on-screen capture remain the
  host's responsibilities, with the contracts to support them already in place.
