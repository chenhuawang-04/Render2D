# ADR: Text Glyph POD Components

Status: Accepted

Date: 2026-06-09

## Context

Stage 9 starts the text pipeline. Render2D must represent text and glyph-derived data as ECS component streams without breaking the existing Strict POD contract. The project is not ready to choose a font library, shaping engine, atlas packer, or Vulkan sampled-image policy yet.

## Decision

Add the Stage 9A Text/Glyph data contract as Strict POD components:

- `Utf8Slice<Provider, Dim>` stores UTF-8 backing-buffer id plus byte offset/count.
- `FontAtlasRef<Provider, Dim>` stores atlas id, generation, texture id, and flags.
- `GlyphRun<Provider, Dim>` stores the source text index, glyph range, atlas id/generation, and flags.
- `GlyphInstance<Provider, Dim>` stores glyph run index, glyph id, atlas rect, position, color, sort key, layer, and flags.

These records are registered in `ComponentTraits` and are valid only for supported Provider/Dim domains. They do not own strings, font resources, glyph cache memory, atlas images, or Vulkan descriptors.

## Alternatives Considered

Use `std::string` or `std::string_view` in `Text`: rejected because components must remain Strict POD and avoid dangling view hazards.

Introduce a font/shaping dependency now: rejected because Stage 9A is the component contract only; dependency choice should be made when shaping and atlas behavior are implemented.

Store atlas/image ownership in `FontAtlasRef`: rejected because ECS components must store references, not resource lifetimes.

## Consequences

- Text data can enter the ECS pipeline through POD ranges and ids.
- Future glyph systems can transform `Text[]` and UTF-8 slices into `GlyphRun[]` and `GlyphInstance[]` without changing component ownership rules.
- The next Stage 9 work should implement deterministic glyph-run building against a test font/cache abstraction before adding real font library integration.
