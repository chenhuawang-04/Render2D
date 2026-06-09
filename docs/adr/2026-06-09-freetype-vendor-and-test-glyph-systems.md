# ADR: FreeType Vendor and Test Glyph Systems

Status: Accepted

Date: 2026-06-09

## Context

Stage 9 needs a path toward real font rendering, and the user provided a local FreeType source tree at `E:/Project/MelosyneTest/VulkanRender_New/freetype-master`. At the same time, Render2D is not ready to choose final UTF-8 decoding, shaping, atlas packing, sampled-image descriptor policy, or Vulkan text draw behavior.

## Decision

Copy the FreeType source snapshot into `third_party/freetype/` for future integration, but keep it dormant. It is not added to CMake, not included from Render2D public headers, and not linked into tests or runtime.

Implement Stage 9B with deterministic dependency-free systems:

- `GlyphRunBuildSystem` maps `Text[]` to `GlyphRun[]` using `FontAtlasRef::font_id` and `Text::utf8_size` as a placeholder glyph count.
- `GlyphInstanceBuildSystem` expands `GlyphRun[]` into `GlyphInstance[]` using `GlyphBuildConfig` for advance, baseline, glyph id base, and atlas-cell layout.

These systems preserve the ECS boundary: components store ids, ranges, atlas rects, positions, colors, sort keys, and flags only.

## Alternatives Considered

Integrate FreeType into CMake immediately: rejected because dependency activation should happen with a dedicated font runtime design and license decision.

Implement real shaping now: rejected because shaping requires a larger text policy and probably HarfBuzz or equivalent decisions.

Delay Stage 9B until real FreeType integration: rejected because deterministic systems provide useful tests for component stream contracts now.

## Consequences

- The repository now contains a dormant third-party FreeType source snapshot.
- Current text systems are suitable for testing pipeline shape, capacity errors, and ECS stream behavior, not for production text layout.
- Before shipping or linking FreeType, the project must document the selected FreeType license path and add focused build/test coverage.
