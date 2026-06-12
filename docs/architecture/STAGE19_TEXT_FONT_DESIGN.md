# Stage 19 — Real Font / Text Pipeline (frozen design)

Status: **implemented end to end** (19A–19F); FreeType + HarfBuzz + SheenBidi vendored and building behind `render2d_font_runtime_support`. The pure systems (19C) stay umbrella-side; shaping/bidi/raster (19D/19E/19F) link the libraries.

This document supersedes the Stage 9 placeholder text pipeline (deterministic, dependency-free) with a real, FreeType + HarfBuzz + bidi pipeline. It is the reference Stage 19 builds against. Stage 19 includes **full bidirectional text in the first version** (no deferral).

## 1. Goal: replace the five Stage 9 placeholders

| # | Stage 9 placeholder | Stage 19 reality |
|---|---|---|
| 1 | `glyph_count == utf8_size` (1 byte = 1 glyph) | UTF-8 decode → codepoints → shaping → N glyphs (N ≠ byte count) |
| 2 | `glyph_id = first + offset` | glyph index from the font `cmap` / shaping |
| 3 | `atlas_rect` = fixed 16-col grid cell | real packed UV from rasterize + atlas placement |
| 4 | `position_x = i · size · 0.5` | per-glyph advance + bearing + offset, pen-walked; bidi reordered |
| 5 | no glyph quad size | per-glyph bitmap width/height |

## 2. External dependencies (hard rule)

Three third-party libraries, each isolated behind the internal target `render2d_font_runtime_support` and **never included from the public umbrella `Render2D.hpp`** (the ThreadCenter pattern). The ECS components in §4 stay free of all three.

| Lib | Role | License |
|---|---|---|
| FreeType | face loading, glyph rasterization, metrics | FTL (chosen) |
| HarfBuzz | shaping one uniform run → glyphs + positions | "Old MIT" (permissive) |
| SheenBidi | UAX#9 bidirectional algorithm + script itemization | Apache-2.0 (vendored at `third_party/sheenbidi`) |

HarfBuzz does **not** do bidi or itemization; a dedicated bidi engine is required for full bidirectional text. SheenBidi is purpose-built (implements UAX#9 including bracket pairing BD16, plus an `SBScriptLocator`), self-contained, and pairs cleanly with HarfBuzz + FreeType.

## 3. POD / runtime boundary (merge rules #11–13)

| In ECS (Strict POD, host-owned streams) | In a runtime (id + generation, never in ECS) |
|---|---|
| `Text`, `TextState`, `TextDirtyRange`, `Utf8Slice` | UTF-8 byte buffers (host memory) |
| `FontRef`, `FontMetrics`, `FontAtlasRef` | `FT_Library`/`FT_Face`, `hb_font`, font file bytes |
| `Codepoint`, `ShapingRun`, `ShapedGlyph`, `GlyphAtlasEntry` | glyph bitmaps, shaping scratch, the glyph atlas image (a Stage 18 atlas) |
| `GlyphRun`, `GlyphInstance` | SheenBidi/HarfBuzz/FreeType objects |

## 4. ECS components (the core)

### 4.1 New (Strict POD; registered in `ComponentTraits`)

```cpp
struct Codepoint      { U32 text_index; U32 codepoint; U32 byte_offset; U32 flags; };
struct ShapingRun     { U32 text_index; U32 codepoint_first; U32 codepoint_count;
                        U32 font_id; U32 script; U32 direction; U32 bidi_level; U32 flags; };
struct ShapedGlyph    { U32 run_index; U32 glyph_id; U32 cluster;
                        float x_advance; float y_advance; float x_offset; float y_offset; U32 flags; };
struct GlyphAtlasEntry{ U32 font_id; U32 generation; U32 glyph_id; float pixel_size;
                        U32 atlas_id; U32 atlas_generation; Aabb2 atlas_rect;
                        float bitmap_width; float bitmap_height; float bearing_x; float bearing_y; U32 flags; };
struct FontMetrics    { U32 font_id; U32 generation; float pixel_size;
                        float ascent; float descent; float line_height; U32 flags; };
```

Direction/script are plain `U32` tags (matching the codebase's `U32` flag style), with constants:
`kTextDirectionLtr = 0`, `kTextDirectionRtl = 1`; script tags mirror SheenBidi/OpenType script ids.

### 4.2 Extended (each a contract change → ADR)

- **`GlyphInstance`** gains `float width; float height;` (after `position_y`) so a glyph is a full textured quad: top-left `(position_x, position_y)`, size `(width, height)`, UV `atlas_rect`.
- **`FontRef`** becomes `{ U32 id; U32 generation; U32 flags; }` (the project's handle convention; was `{ id }`).

### 4.3 Deferred (planned, not in 19B)

- **`Text` / `TextState` `content_version`** — detects in-place byte edits that don't change the slice. Orthogonal to bidi and touches the Stage 9 `TextDirtySystem`; added in a later increment to keep the contract freeze low-risk.

## 5. Decomposed pipeline

Shaping is **five systems + three runtime touchpoints**, not a monolith. Only stages that need a font object or a third-party engine are runtime-side; their outputs are POD so everything downstream is pure and unit-testable.

| Stage | Kind | Target | Output |
|---|---|---|---|
| `Utf8DecodeSystem` | pure system | umbrella | `Codepoint[]` (bytes arrive as a `std::span<const U8>` arg) |
| `BidiItemizeStage` | dep-backed | font-runtime | `ShapingRun[]` (UAX#9 levels + reorder + script via SheenBidi) |
| `FontShapeRuntime::shape` | runtime | font-runtime | `ShapedGlyph[]` (HarfBuzz `hb_shape` per run) |
| `GlyphAtlasRuntime::ensureResident` | runtime | font-runtime | `GlyphAtlasEntry[]` (FreeType raster → Stage 18 atlas upload) |
| `GlyphPositionSystem` | pure system | umbrella | `GlyphInstance[]` (pen-walk advances + bearings; line layout) |
| `GlyphInstanceToSpriteSystem` | pure system | umbrella | `SpriteInstance[]` (bridge → Stage 12–18 GPU path) |
| `GlyphBatchSystem` (exists) | pure system | umbrella | `DrawCommand[]` |

```
Text[] + bytes        --Utf8DecodeSystem(pure)--------------> Codepoint[]
Codepoint[]           --BidiItemizeStage(SheenBidi)---------> ShapingRun[]      (visual order, per line)
ShapingRun[]+font     --FontShapeRuntime(HarfBuzz)----------> ShapedGlyph[]
ShapedGlyph[](ids)    --GlyphAtlasRuntime(FreeType+Stage18)-> GlyphAtlasEntry[] (+ atlas uploads)
ShapedGlyph[]+entries+FontMetrics --GlyphPositionSystem(pure)--> GlyphInstance[]
GlyphInstance[]       --GlyphInstanceToSpriteSystem(pure)----> SpriteInstance[]
GlyphRun[]+...        --GlyphBatchSystem(pure)--------------> DrawCommand[] --> sprite packet path (reuse)
```

## 6. The two/three runtime touchpoints (POD in / POD out)

```cpp
// render2d_font_runtime_support (links FreeType+HarfBuzz+SheenBidi; not in umbrella)
NativeResult loadFont(U32 font_id, std::span<const U8> font_file_bytes, FontRef& out);  // FT_New_Memory_Face + hb_ft_font

NativeResult itemizeBidi(std::span<const Codepoint>, std::span<ShapingRun> out);         // SheenBidi UAX#9 + script

NativeResult shape(std::span<const ShapingRun>, std::span<const Codepoint>,
                   std::span<ShapedGlyph> out);                                          // hb_shape per run

NativeResult ensureResident(std::span<const ShapedGlyph>, /*font/size key*/,
                            std::span<GlyphAtlasEntry> out,
                            VulkanTextureAtlasRuntime&, VulkanResourceRuntime&, VkCommandBuffer);
```

Each takes a non-owning `std::span` of POD in, writes a `std::span` of POD out — identical in spirit to the pure systems, so they remain deterministic and testable; only their *implementation* links the libraries.

## 7. Rendering

- **Bridge to SpriteInstance** (chosen): `GlyphInstanceToSpriteSystem` builds the 2×3 transform from `(position_x, position_y, width, height)` and copies `atlas_rect` → `uv_min/max`, `texture_id` ← the glyph atlas texture. Text then rides the proven Stage 12–18 sprite path; no second GPU path.
- **Glyph atlas** is a Stage 18 `VulkanTextureAtlasRuntime` atlas in `VK_FORMAT_R8_UNORM` (8-bit coverage). Requires adding the 1-byte-per-pixel case to `VulkanResourceRuntime`'s format table (`formatBytesPerPixel`/`makeImageByteCount`/`makeRegionByteCount`).
- **Glyph shader**: fragment shader sampling `.r` as coverage — `out = vec4(instanceColor.rgb, instanceColor.a * tex.r)`. Embedded SPIR-V like `SpriteShaders.hpp`; the text material maps to this glyph pipeline via `SpriteMaterialBinding`.

## 8. Bidirectional text (UAX#9 via SheenBidi)

`BidiItemizeStage` runs the Unicode Bidirectional Algorithm per paragraph: resolve embedding levels (explicit isolates/overrides + implicit weak/neutral/bracket rules), then produce level runs, then reorder runs into **visual order** per line (L2). Each `ShapingRun` carries its `bidi_level` (even = LTR, odd = RTL) and `direction`; RTL runs are shaped right-to-left by HarfBuzz. Mirrored glyphs (brackets) are handled by shaping. Line breaking interacts with reordering (reorder happens per line after breaking), so line layout lives in `GlyphPositionSystem` consuming `FontMetrics`.

## 9. Incremental/dirty model change

Stage 9 assumed `glyph_count == utf8_size` (stable), so `TextDirtySystem` computed glyph ranges. Real shaping makes glyph counts **dynamic**, so range allocation moves out of `TextDirtySystem` (which keeps only *which texts are dirty*) to a runtime-side glyph-range allocator that rewrites `GlyphRun.glyph_first/count` after shaping. Stage 19 starts by re-shaping dirty texts and repacking ranges; stable-slot incrementality is a later refinement.

## 10. Sub-stages and the vendoring prerequisite

- **19A** Vendor + CMake FreeType (present), **HarfBuzz**, **SheenBidi** behind options; internal `render2d_font_runtime_support` target (umbrella stays lib-free); licenses documented. **Status: DONE.** All three are vendored under `third_party/` and build as static libraries behind `RENDER2D_BUILD_FONT_RUNTIME` (default ON): FreeType self-contained (zlib/png/bzip2/brotli/harfbuzz off), HarfBuzz core shaping + FreeType interop (subset/raster/vector/gpu/glib/icu off), SheenBidi unity build. Their include dirs are promoted to SYSTEM so they do not trip Render2D's `-Werror`.
- **19B** ECS contracts: new POD components + `GlyphInstance`/`FontRef` extensions + traits + POD tests + ADR. **(dependency-free — implemented)**
- **19C** `Utf8DecodeSystem` (pure) + `GlyphPositionSystem` (pure) + `GlyphInstanceToSpriteSystem` (pure) + unit tests. **(dependency-free — implemented; single-baseline layout, multi-line/per-line bidi reorder layered on once the itemizer lands)**
- **19D** `FontShapeRuntime` (FreeType face load + `hb-ft` + `hb_shape`). **(implemented — `include/Render2D/Font/FontShapeRuntime.hpp`; FT_Face/hb_font behind a FontRef slot table, `loadFontFromMemory` + `shape`; smoke shapes a system font with graceful skip)**
- **19E** `BidiItemizeStage` (SheenBidi UAX#9 + script). **(implemented — `include/Render2D/Font/BidiItemizeRuntime.hpp`; visually-ordered single-script `ShapingRun[]`, full bidi, unit-tested)**
- **19F** `GlyphAtlasRuntime` (FreeType raster → Stage 18, R8 support) + glyph shader + offscreen smoke rendering a bidi string, readback; closeout/ADR/gate. **(implemented — `include/Render2D/Font/GlyphAtlasRuntime.hpp` + `R8_UNORM` in `VulkanResourceRuntime` + `kGlyphCoverageFragSpv` premultiplied coverage shader + `tests/vulkan_glyph_text_render_test.cpp` rendering "Hi" end to end on GPU)**

Stage 19 is implemented and verified end to end (Debug + Perf gates green). The `GlyphInstanceToSpriteSystem` bridge now also carries an optional viewport projection (layout pixels → clip space) used by the on-GPU render path.
