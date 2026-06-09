# ADR: Stage 9 Text Dirty Glyph Pipeline

Status: Accepted

Date: 2026-06-09

## Context

Stage 9 must complete the ECS-facing text path without committing to a production font runtime. The project already has Strict POD `Text`, `FontAtlasRef`, `GlyphRun`, and `GlyphInstance` contracts plus deterministic placeholder glyph systems. The remaining requirement is to avoid rebuilding static text every frame and to route glyph instances into the existing draw/batch command path.

## Decision

Add two ECS-owned POD text tracking components:

- `TextState` stores the previous text-facing fields plus the computed glyph range.
- `TextDirtyRange` identifies changed text indices and their previous/new glyph ranges.

Add dependency-free systems:

- `TextDirtySystem` compares `Text[]` with previous `TextState[]`, writes next `TextState[]`, and emits dirty ranges.
- `GlyphRunBuildSystem::runDirty` updates only dirty `GlyphRun[]` entries.
- `GlyphInstanceBuildSystem::runDirty` updates only dirty `GlyphInstance[]` ranges.
- `GlyphBatchSystem` converts `GlyphRun[]` plus `FontAtlasRef[]` into regular `DrawCommand[]` entries, so glyphs enter the existing `BatchSystem`.

FreeType remains dormant in `third_party/freetype/` and is not linked by this decision.

## Alternatives Considered

Rebuild all glyph runs and instances every frame: rejected because it violates the static-text requirement.

Introduce a private renderer-side text cache outside ECS: rejected for this stage because the host engine will eventually own ECS component storage.

Integrate FreeType now: rejected because real decoding, shaping, rasterization, atlas packing, descriptor policy, and license choice require a separate runtime design.

## Consequences

- Stage 9 now has a complete dependency-free ECS text pipeline.
- Static text can skip glyph rebuild work when `TextState` is preserved by the host ECS.
- Glyph rendering follows the same `DrawCommand[] -> BatchCommand[]` pipeline as sprites.
- Placeholder glyph IDs/atlas rects remain non-production; real font runtime work is still required later.
