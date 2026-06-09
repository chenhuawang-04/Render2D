# ADR: fast_math POD Math Types

Status: Accepted

Date: 2026-06-09

## Context

Render2D had small local math structs for `Aabb2` and `Affine2X3` while the project already depends on `E:/Project/MelosyneTest/Math/fast_math`. The host-engine requirement is that render math must use fast_math everywhere, while ECS-visible fields must remain Strict POD.

The Stage 10B benchmark also showed `BoundsSystem` dominating sprite and mixed CPU baselines because bounds were transformed through a local four-corner implementation.

## Decision

Render2D math aliases now map directly to fast_math POD types:

- `Render2D::Vec2 = MMath::Vec2`
- `Render2D::Mat3 = MMath::Mat3`
- `Render2D::Aabb2 = MMath::Aabb2`

The custom Render2D `Aabb2` and `Affine2X3` structs are removed. `WorldTransform::affine` stores `Mat3`. Bounds, culling, transform, and text atlas-rect systems use fast_math free functions and wrappers. AABB construction and access use `makeAabb2`, `aabb2Min`, and `aabb2Max` because fast_math stores maximum coordinates internally as negative values.

`BoundsSystem` now transforms AABBs through center/extents over `MMath::Mat3`, using fast_math `abs`, `mat3TransformPoint`, and `aabb2FromCenterExtents`.

## Alternatives Considered

Keep Render2D local structs and convert at system boundaries: rejected because it preserves a second math representation and invites drift.

Keep an `Affine2X3` compatibility alias to `MMath::Mat3`: rejected because the user explicitly requested abandoning the old math layer and a clean migration is acceptable.

Use fast_math `aabb2Transform` directly: rejected for `BoundsSystem` hot path because it still performs four-corner work; the center/extents formula is equivalent for affine AABB transforms and matches the benchmark objective.

## Consequences

- ECS component fields still satisfy Strict POD requirements.
- Consumers must treat `Aabb2` as an opaque fast_math POD and use accessors for max coordinates.
- `WorldTransform` is 36 bytes instead of the old six-float affine payload; future GPU instance upload may pack into a separate upload format if needed.
- Stage 10C local Debug benchmark reduced 10k sprite bounds time from about 3.64 ms to about 0.55 ms.
- Any future math operation in Render2D should call fast_math free functions or add a narrow wrapper in `Core/Types.hpp`; do not add new Render2D-local math structs.
