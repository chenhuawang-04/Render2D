# ADR: Stage 19 Real Font/Text Pipeline

Date: 2026-06-11

## Status

Accepted. Stage 19 is implemented end to end (19A–19F): the ECS contracts (19B), the three pure systems (19C), the dependency wiring (19A — FreeType + HarfBuzz + SheenBidi as git submodules building behind `render2d_font_runtime_support`), the SheenBidi bidi+script itemizer (19E), the HarfBuzz/FreeType shaping runtime (19D), and the FreeType-raster glyph atlas residency + coverage shader + offscreen bidi render (19F). Both the Debug and Perf gates pass.

## Context

Stage 9 froze the *shape* of the text pipeline with deterministic placeholders (1 byte = 1 glyph, sequential glyph ids, fixed grid atlas cells, uniform advance, no glyph quad size). Stage 19 replaces those with a real pipeline and, per project direction, includes **full bidirectional text in the first version**. Real shaping needs the raw UTF-8 bytes plus a font face (HarfBuzz `hb_font` over `FT_Face`), and full bidi needs the Unicode Bidirectional Algorithm (UAX#9) — none of which are POD components. The full design is in `docs/architecture/STAGE19_TEXT_FONT_DESIGN.md`.

## Decision

- **Decompose** shaping into five pure systems + three runtime touchpoints (decode, position, bridge, glyph-batch are pure; bidi-itemize, shape, atlas-residency link the third-party libraries). Each runtime touchpoint takes POD `std::span` in and writes POD `std::span` out, keeping everything downstream deterministic and testable.
- **ECS stays Strict POD.** Add POD components `Codepoint`, `ShapingRun`, `ShapedGlyph`, `GlyphAtlasEntry`, `FontMetrics`; extend `GlyphInstance` with `width`/`height` (a glyph is now a full textured quad) and `FontRef` to `{ id, generation, flags }` (the project handle convention). Font faces, `hb_font`, glyph bitmaps, shaping scratch, and the glyph atlas image live in runtimes, referenced by id + generation.
- **Reuse the GPU path.** A pure `GlyphInstanceToSpriteSystem` bridges `GlyphInstance[]` → `SpriteInstance[]`, so text renders through the Stage 12–18 sprite path. The glyph atlas is a Stage 18 `VulkanTextureAtlasRuntime` atlas in `R8_UNORM`; a coverage (`.r`) fragment shader is the text material.
- **Isolate dependencies.** FreeType (FTL), HarfBuzz (Old MIT), and SheenBidi (UAX#9 + script) build behind the internal `render2d_font_runtime_support` target and are never included from the public umbrella `Render2D.hpp` — the established ThreadCenter pattern.
- **Dynamic glyph counts.** Real glyph counts are only known after shaping, so glyph-range allocation moves out of `TextDirtySystem` (which keeps detecting *which* texts are dirty) into a runtime-side allocator that rewrites `GlyphRun` ranges post-shape.

## Consequences

- The ECS text contract grows but stays Strict POD and host-owned; the placeholder Stage 9 glyph fabrication (`GlyphRunBuildSystem`/`GlyphInstanceBuildSystem` faking glyphs from byte counts) is superseded by the real decode→itemize→shape→position chain.
- Stage 19 takes on three dependencies, all git submodules under `third_party/` (pinned `VER-2-14-3` / `14.2.1` / `v3.0.0`) and building as static libraries behind `RENDER2D_BUILD_FONT_RUNTIME` (default ON): FreeType (FTL, self-contained), HarfBuzz (Old MIT, core shaping + FreeType interop), and SheenBidi (Apache-2.0, unity build). They are reachable only through the internal `render2d_font_runtime_support` target and never from the umbrella; their include dirs are promoted to SYSTEM so they do not trip Render2D's `-Werror`. The submodule-acquisition contract is recorded in `docs/adr/2026-06-12-third-party-submodules.md`. All of 19A–19F are implemented and verified; the glyph atlas is an R8_UNORM Stage 18 atlas and text renders through the existing sprite path via a premultiplied coverage shader, with the `GlyphInstanceToSpriteSystem` bridge applying an optional layout→clip viewport projection.
- The glyph atlas uses a deterministic shelf packer and re-rasterizes per (font, glyph, pixel size); skyline/MaxRects packing, glyph-cache eviction/retirement, soft-wrap line breaking, and stable-slot dirty incrementality are later refinements on top of this working pipeline.
- `Text`/`TextState` `content_version` (in-place edit detection) is planned but deferred from the contract freeze to avoid destabilizing the Stage 9 `TextDirtySystem`.
