#pragma once

#include "Render2D/Component/Sprite.hpp"
#include "Render2D/Component/Text.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Core/Types.hpp"
#include "Render2D/Meta/Domain.hpp"

#include <limits>
#include <span>

namespace Render2D {

// Non-owning view of host-owned UTF-8 bytes for one buffer id. This is a system
// input argument, not an ECS component (it carries a raw, non-owning pointer);
// it mirrors how component spans are passed but for the raw text bytes.
struct Utf8BufferView {
    U32 buffer_id;
    const U8* bytes;
    U32 byte_count;
};

inline constexpr U32 kCodepointInvalidFlag = 1U << 0U;
inline constexpr U32 kUnicodeReplacement = 0xFFFDU;

// Stage 19C: decodes each Text's UTF-8 slice into a flat Codepoint[] stream.
// Pure system - the bytes arrive as a non-owning span, with no font face or
// third-party library involved. Malformed/overlong/surrogate/truncated input
// emits U+FFFD (flagged) and resynchronizes one byte at a time. `byte_offset`
// is relative to the text's `utf8_offset`, matching shaper cluster indices.
template<class Provider, class Dim>
struct Utf8DecodeSystem {
    static SystemResult run(
        std::span<const Text<Provider, Dim>> texts_,
        std::span<const Utf8BufferView> buffers_,
        std::span<Codepoint<Provider, Dim>> out_codepoints_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (texts_.size() > (std::numeric_limits<U32>::max)()) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }

            U32 write_count = 0U;
            for (Usize index = 0U; index < texts_.size(); ++index) {
                const auto& text = texts_[index];
                const Utf8BufferView* buffer = findBuffer(text.utf8_buffer_id, buffers_);
                if (buffer == nullptr ||
                    text.utf8_offset > buffer->byte_count ||
                    text.utf8_size > buffer->byte_count - text.utf8_offset) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(index),
                        .write_count = write_count,
                    };
                }

                const U8* slice = buffer->bytes + text.utf8_offset;
                U32 cursor = 0U;
                while (cursor < text.utf8_size) {
                    U32 codepoint = 0U;
                    U32 consumed = 0U;
                    const bool valid = decodeOne(slice + cursor, text.utf8_size - cursor, codepoint, consumed);
                    if (static_cast<Usize>(write_count) >= out_codepoints_.size()) {
                        return {
                            .code = SystemStatusCode::InsufficientCapacity,
                            .read_count = static_cast<U32>(index),
                            .write_count = write_count,
                        };
                    }
                    out_codepoints_[write_count] = {
                        .text_index = static_cast<U32>(index),
                        .codepoint = valid ? codepoint : kUnicodeReplacement,
                        .byte_offset = cursor,
                        .flags = valid ? 0U : kCodepointInvalidFlag,
                    };
                    ++write_count;
                    cursor += consumed;
                }
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(texts_.size()),
                .write_count = write_count,
            };
        }
    }

private:
    static const Utf8BufferView* findBuffer(
        U32 buffer_id_,
        std::span<const Utf8BufferView> buffers_) noexcept
    {
        for (const auto& buffer : buffers_) {
            if (buffer.buffer_id == buffer_id_) {
                return &buffer;
            }
        }
        return nullptr;
    }

    // Decodes one UTF-8 scalar value. On any malformed/overlong/surrogate/
    // truncated sequence, reports invalid and consumes exactly one byte so the
    // caller emits U+FFFD and resynchronizes.
    static bool decodeOne(const U8* data_, U32 available_, U32& out_codepoint_, U32& out_consumed_) noexcept
    {
        out_consumed_ = 1U;
        out_codepoint_ = 0U;

        const U8 lead = data_[0];
        if (lead < 0x80U) {
            out_codepoint_ = lead;
            return true;
        }

        U32 length = 0U;
        U32 codepoint = 0U;
        U32 min_codepoint = 0U;
        if ((lead & 0xE0U) == 0xC0U) {
            length = 2U;
            codepoint = lead & 0x1FU;
            min_codepoint = 0x80U;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3U;
            codepoint = lead & 0x0FU;
            min_codepoint = 0x800U;
        } else if ((lead & 0xF8U) == 0xF0U) {
            length = 4U;
            codepoint = lead & 0x07U;
            min_codepoint = 0x10000U;
        } else {
            return false;
        }

        if (available_ < length) {
            return false;
        }
        for (U32 offset = 1U; offset < length; ++offset) {
            const U8 continuation = data_[offset];
            if ((continuation & 0xC0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | static_cast<U32>(continuation & 0x3FU);
        }
        if (codepoint < min_codepoint ||
            codepoint > 0x10FFFFU ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
            return false;
        }

        out_codepoint_ = codepoint;
        out_consumed_ = length;
        return true;
    }
};

// Set on a GlyphInstance whose shaped glyph had no resident GlyphAtlasEntry.
// Whitespace (a glyph with a 0x0 bitmap) and not-yet-resident glyphs both land
// here; the quad is emitted with zero size so it still advances the pen but
// draws nothing.
inline constexpr U32 kGlyphInstanceMissingAtlasFlag = 1U << 0U;

// Layout origin for a single text baseline, in text-layout space (y-down: the
// origin is the top-left of the layout box, y grows downward, the pen advances
// in +x). The pen starts at origin_x; glyph quad tops are measured down from
// baseline_y using each glyph's bearing.
struct GlyphLayoutConfig {
    float origin_x;
    float baseline_y;
    U32 flags;
};

// Stage 19C: pen-walks shaped glyphs (already in visual order from the bidi +
// shaping stages) into positioned GlyphInstance quads. Pure system - the
// advances/offsets arrive in ShapedGlyph, the per-glyph bitmap size, bearings,
// and UV arrive in GlyphAtlasEntry, and color/layer come from the owning Text.
// No font face or third-party library is involved.
//
// Scope: single baseline. Multi-line layout (line breaking, per-line bidi
// reordering, FontMetrics line advance) depends on line-break metadata that the
// dependency-backed itemizer produces, and is layered on top once that lands.
template<class Provider, class Dim>
struct GlyphPositionSystem {
    static SystemResult run(
        std::span<const ShapedGlyph<Provider, Dim>> shaped_glyphs_,
        std::span<const ShapingRun<Provider, Dim>> runs_,
        std::span<const Text<Provider, Dim>> texts_,
        std::span<const GlyphAtlasEntry<Provider, Dim>> atlas_entries_,
        GlyphLayoutConfig config_,
        std::span<GlyphInstance<Provider, Dim>> out_glyphs_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isSystemResultCountRepresentable(shaped_glyphs_.size()) ||
                !isSystemResultCountRepresentable(runs_.size()) ||
                !isSystemResultCountRepresentable(texts_.size()) ||
                !isSystemResultCountRepresentable(atlas_entries_.size()) ||
                !isSystemResultCountRepresentable(out_glyphs_.size())) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            if (shaped_glyphs_.empty()) {
                return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 0U};
            }
            if (out_glyphs_.size() < shaped_glyphs_.size()) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = 0U,
                    .write_count = 0U,
                };
            }
            for (const auto& shaped : shaped_glyphs_) {
                if (shaped.run_index >= runs_.size() ||
                    runs_[shaped.run_index].text_index >= texts_.size()) {
                    return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
                }
            }

            float pen_x = config_.origin_x;
            float pen_y = config_.baseline_y;
            const U32 glyph_count = static_cast<U32>(shaped_glyphs_.size());
            for (U32 index = 0U; index < glyph_count; ++index) {
                const auto& shaped = shaped_glyphs_[index];
                const auto& run = runs_[shaped.run_index];
                const auto& text = texts_[run.text_index];
                const auto* entry = findAtlasEntry(run.font_id, shaped.glyph_id, text.pixel_size, atlas_entries_);

                const float quad_x = pen_x + shaped.x_offset + (entry != nullptr ? entry->bearing_x : 0.0F);
                const float quad_y = pen_y - shaped.y_offset - (entry != nullptr ? entry->bearing_y : 0.0F);

                out_glyphs_[index] = {
                    .glyph_run_index = shaped.run_index,
                    .glyph_id = shaped.glyph_id,
                    .atlas_rect = entry != nullptr ? entry->atlas_rect : makeAabb2(0.0F, 0.0F, 0.0F, 0.0F),
                    .position_x = quad_x,
                    .position_y = quad_y,
                    .width = entry != nullptr ? entry->bitmap_width : 0.0F,
                    .height = entry != nullptr ? entry->bitmap_height : 0.0F,
                    .color_rgba8 = text.color_rgba8,
                    .sort_key = text.layer,
                    .layer = text.layer,
                    .flags = entry != nullptr ? 0U : kGlyphInstanceMissingAtlasFlag,
                };

                pen_x += shaped.x_advance;
                pen_y += shaped.y_advance;
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = glyph_count,
                .write_count = glyph_count,
            };
        }
    }

private:
    static const GlyphAtlasEntry<Provider, Dim>* findAtlasEntry(
        U32 font_id_,
        U32 glyph_id_,
        float pixel_size_,
        std::span<const GlyphAtlasEntry<Provider, Dim>> atlas_entries_) noexcept
    {
        for (const auto& entry : atlas_entries_) {
            if (entry.font_id == font_id_ &&
                entry.glyph_id == glyph_id_ &&
                entry.pixel_size == pixel_size_) {
                return &entry;
            }
        }
        return nullptr;
    }
};

// Texture/material binding for one glyph atlas, supplied to the bridge below.
// Text rides the sprite path one binding (atlas) at a time, mirroring how the
// sprite encoder draws one texture per packet. When viewport_width/height are
// positive, glyph layout coordinates (pixels, y-down) are projected to clip
// space [-1,1]; when zero, the layout rectangle is emitted verbatim (identity).
struct GlyphSpriteBridgeConfig {
    U32 texture_id;
    U32 texture_generation;
    U32 material_id;
    U32 material_generation;
    float viewport_width;
    float viewport_height;
    U32 flags;
};

// Stage 19C: bridges positioned GlyphInstance quads onto the sprite GPU path by
// emitting one SpriteInstance per visible glyph. Pure system. The 2x3 affine
// maps the unit quad [0,1]x[0,1] onto the glyph's layout rectangle
// (position_x, position_y) + (width, height), and atlas_rect becomes the UV
// rectangle; the texture/material binding is the glyph atlas from config_.
// Degenerate glyphs (zero width or height, e.g. whitespace or non-resident)
// draw nothing and are skipped, so write_count counts only visible glyphs.
template<class Provider, class Dim>
struct GlyphInstanceToSpriteSystem {
    static SystemResult run(
        std::span<const GlyphInstance<Provider, Dim>> glyphs_,
        GlyphSpriteBridgeConfig config_,
        std::span<SpriteInstance<Provider, Dim>> out_instances_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isSystemResultCountRepresentable(glyphs_.size()) ||
                !isSystemResultCountRepresentable(out_instances_.size())) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            if (glyphs_.empty()) {
                return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 0U};
            }

            const U32 glyph_count = static_cast<U32>(glyphs_.size());
            U32 write_count = 0U;
            for (U32 index = 0U; index < glyph_count; ++index) {
                const auto& glyph = glyphs_[index];
                if (glyph.width <= 0.0F || glyph.height <= 0.0F) {
                    continue;
                }
                if (static_cast<Usize>(write_count) >= out_instances_.size()) {
                    return {
                        .code = SystemStatusCode::InsufficientCapacity,
                        .read_count = index,
                        .write_count = write_count,
                    };
                }

                const Vec2 uv_min = Render2D::aabb2Min(glyph.atlas_rect);
                const Vec2 uv_max = Render2D::aabb2Max(glyph.atlas_rect);
                const bool project = config_.viewport_width > 0.0F && config_.viewport_height > 0.0F;
                const float scale_x = project ? 2.0F / config_.viewport_width : 1.0F;
                const float scale_y = project ? 2.0F / config_.viewport_height : 1.0F;
                const float bias = project ? -1.0F : 0.0F;
                out_instances_[write_count] = {
                    .transform_m00 = glyph.width * scale_x,
                    .transform_m01 = 0.0F,
                    .transform_m02 = glyph.position_x * scale_x + bias,
                    .transform_m10 = 0.0F,
                    .transform_m11 = glyph.height * scale_y,
                    .transform_m12 = glyph.position_y * scale_y + bias,
                    .uv_min_x = uv_min.x,
                    .uv_min_y = uv_min.y,
                    .uv_max_x = uv_max.x,
                    .uv_max_y = uv_max.y,
                    .source_index = write_count,
                    .source_id = glyph.glyph_run_index,
                    .texture_id = config_.texture_id,
                    .texture_generation = config_.texture_generation,
                    .material_id = config_.material_id,
                    .material_generation = config_.material_generation,
                    .color_rgba8 = glyph.color_rgba8,
                    .sort_key = glyph.sort_key,
                    .layer = glyph.layer,
                    .flags = glyph.flags,
                    .sampler_index = 0U,
                };
                ++write_count;
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = glyph_count,
                .write_count = write_count,
            };
        }
    }
};

} // namespace Render2D
