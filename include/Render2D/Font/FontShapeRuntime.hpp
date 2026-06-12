#pragma once

// Stage 19D: text shaping via HarfBuzz over FreeType faces.
//
// Part of the font/text runtime; links FreeType + HarfBuzz. NOT included by the
// public umbrella `Render2D.hpp` -- consumers link `render2d_font_runtime_support`.
// Owns FT_Face + hb_font_t per font behind a FontRef id + generation slot table
// (the project's native-runtime pattern). `shape` is POD `std::span` in / out so
// the positioning + bridge stages downstream stay pure.
//
// Lifetime: FreeType does not copy the font file bytes (FT_New_Memory_Face keeps
// a pointer into them), so the caller must keep each loaded font's byte buffer
// alive until the font is released or the runtime shuts down.

#include "Render2D/Component/Text.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Core/Types.hpp"
#include "Render2D/Memory/RenderVector.hpp"
#include "Render2D/Meta/Domain.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>
#include <hb-ft.h>

#include <cstdint>
#include <span>

namespace Render2D {

// Describes one rasterized glyph bitmap. `pixels` is 8-bit coverage (gray) with
// `pitch` bytes per row, owned by the FreeType glyph slot and only valid until
// the next rasterize on the same runtime, so callers must copy immediately.
struct FontGlyphBitmap {
    U32 width;
    U32 height;
    I32 bearing_x;
    I32 bearing_y;
    float advance_x;
    const U8* pixels;
    U32 pitch;
};

template<class Provider, class Dim>
class FontShapeRuntime {
public:
    SystemResult initialize()
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (library != nullptr) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            if (FT_Init_FreeType(&library) != 0) {
                library = nullptr;
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            shape_buffer = hb_buffer_create();
            if (hb_buffer_allocation_successful(shape_buffer) == 0) {
                shutdown();
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 0U};
        }
    }

    void shutdown()
    {
        for (auto& slot : slots) {
            destroySlot(slot);
        }
        slots.clear();
        if (shape_buffer != nullptr) {
            hb_buffer_destroy(shape_buffer);
            shape_buffer = nullptr;
        }
        if (library != nullptr) {
            FT_Done_FreeType(library);
            library = nullptr;
        }
    }

    ~FontShapeRuntime()
    {
        shutdown();
    }

    FontShapeRuntime() = default;
    FontShapeRuntime(const FontShapeRuntime&) = delete;
    FontShapeRuntime& operator=(const FontShapeRuntime&) = delete;
    FontShapeRuntime(FontShapeRuntime&&) = delete;
    FontShapeRuntime& operator=(FontShapeRuntime&&) = delete;

    // Loads a face from caller-owned font bytes and registers it under font_id_
    // (the id Text/ShapingRun reference). The bytes must outlive the font.
    SystemResult loadFontFromMemory(
        U32 font_id_,
        std::span<const U8> font_bytes_,
        float pixel_size_,
        FontRef<Provider, Dim>& out_ref_,
        FontMetrics<Provider, Dim>& out_metrics_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (library == nullptr || font_bytes_.empty() || pixel_size_ <= 0.0F) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }

            FT_Face face = nullptr;
            const FT_Error new_face_error = FT_New_Memory_Face(
                library,
                reinterpret_cast<const FT_Byte*>(font_bytes_.data()),
                static_cast<FT_Long>(font_bytes_.size()),
                0,
                &face);
            if (new_face_error != 0 || face == nullptr) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            if (FT_Set_Pixel_Sizes(face, 0U, static_cast<FT_UInt>(pixel_size_)) != 0) {
                FT_Done_Face(face);
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            hb_font_t* hb_font = hb_ft_font_create_referenced(face);
            if (hb_font == nullptr) {
                FT_Done_Face(face);
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }

            FontSlot& slot = acquireSlot(font_id_);
            slot.face = face;
            slot.hb_font = hb_font;
            slot.font_id = font_id_;
            slot.pixel_size = pixel_size_;
            slot.occupied = true;

            out_ref_ = {.id = font_id_, .generation = slot.generation, .flags = 0U};
            out_metrics_ = {
                .font_id = font_id_,
                .generation = slot.generation,
                .pixel_size = pixel_size_,
                .ascent = static_cast<float>(face->size->metrics.ascender) / 64.0F,
                .descent = static_cast<float>(face->size->metrics.descender) / 64.0F,
                .line_height = static_cast<float>(face->size->metrics.height) / 64.0F,
                .flags = 0U,
            };
            return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 1U};
        }
    }

    // Shapes each ShapingRun (single script + direction) with HarfBuzz, emitting
    // one ShapedGlyph per output glyph (glyph index + cluster + advances/offsets
    // in pixels). out_shaped_ must hold all produced glyphs.
    SystemResult shape(
        std::span<const ShapingRun<Provider, Dim>> runs_,
        std::span<const Codepoint<Provider, Dim>> codepoints_,
        std::span<ShapedGlyph<Provider, Dim>> out_shaped_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (library == nullptr || shape_buffer == nullptr ||
                !isSystemResultCountRepresentable(runs_.size()) ||
                !isSystemResultCountRepresentable(codepoints_.size()) ||
                !isSystemResultCountRepresentable(out_shaped_.size())) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }

            U32 write_count = 0U;
            const U32 run_count = static_cast<U32>(runs_.size());
            for (U32 run_index = 0U; run_index < run_count; ++run_index) {
                const auto& run = runs_[run_index];
                const Usize run_last = static_cast<Usize>(run.codepoint_first) + run.codepoint_count;
                if (run_last > codepoints_.size()) {
                    return {.code = SystemStatusCode::InvalidInput, .read_count = run_index, .write_count = write_count};
                }
                const FontSlot* slot = findSlot(run.font_id);
                if (slot == nullptr) {
                    return {.code = SystemStatusCode::InvalidInput, .read_count = run_index, .write_count = write_count};
                }
                if (run.codepoint_count == 0U) {
                    continue;
                }

                codepoint_scratch.resize(run.codepoint_count);
                for (U32 index = 0U; index < run.codepoint_count; ++index) {
                    codepoint_scratch[index] = static_cast<std::uint32_t>(codepoints_[run.codepoint_first + index].codepoint);
                }

                hb_buffer_clear_contents(shape_buffer);
                hb_buffer_add_utf32(
                    shape_buffer,
                    codepoint_scratch.data(),
                    static_cast<int>(run.codepoint_count),
                    0U,
                    static_cast<int>(run.codepoint_count));
                hb_buffer_guess_segment_properties(shape_buffer);
                hb_buffer_set_direction(
                    shape_buffer,
                    run.direction == kTextDirectionRtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
                hb_shape(slot->hb_font, shape_buffer, nullptr, 0U);

                unsigned int glyph_count = 0U;
                const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(shape_buffer, &glyph_count);
                const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(shape_buffer, &glyph_count);
                if (static_cast<Usize>(write_count) + glyph_count > out_shaped_.size()) {
                    return {.code = SystemStatusCode::InsufficientCapacity, .read_count = run_index, .write_count = write_count};
                }
                for (unsigned int glyph_index = 0U; glyph_index < glyph_count; ++glyph_index) {
                    out_shaped_[write_count] = {
                        .run_index = run_index,
                        .glyph_id = static_cast<U32>(infos[glyph_index].codepoint),
                        .cluster = static_cast<U32>(infos[glyph_index].cluster),
                        .x_advance = static_cast<float>(positions[glyph_index].x_advance) / 64.0F,
                        .y_advance = static_cast<float>(positions[glyph_index].y_advance) / 64.0F,
                        .x_offset = static_cast<float>(positions[glyph_index].x_offset) / 64.0F,
                        .y_offset = static_cast<float>(positions[glyph_index].y_offset) / 64.0F,
                        .flags = 0U,
                    };
                    ++write_count;
                }
            }

            return {.code = SystemStatusCode::Ok, .read_count = run_count, .write_count = write_count};
        }
    }

    [[nodiscard]] U32 fontCount() const noexcept
    {
        U32 count = 0U;
        for (const auto& slot : slots) {
            if (slot.occupied) {
                ++count;
            }
        }
        return count;
    }

    // Rasterizes a glyph (by font glyph index) into an 8-bit coverage bitmap.
    // The returned `pixels` point into the FreeType glyph slot and are only
    // valid until the next rasterize on this runtime. Whitespace glyphs render
    // to a zero-size bitmap (pixels == nullptr), which is normal.
    SystemResult rasterizeGlyph(U32 font_id_, U32 glyph_id_, FontGlyphBitmap& out_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (library == nullptr) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            FontSlot* slot = findSlotMutable(font_id_);
            if (slot == nullptr) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            if (FT_Load_Glyph(slot->face, static_cast<FT_UInt>(glyph_id_), FT_LOAD_RENDER) != 0) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }

            const FT_GlyphSlot glyph = slot->face->glyph;
            const int pitch = glyph->bitmap.pitch;
            out_ = {
                .width = glyph->bitmap.width,
                .height = glyph->bitmap.rows,
                .bearing_x = static_cast<I32>(glyph->bitmap_left),
                .bearing_y = static_cast<I32>(glyph->bitmap_top),
                .advance_x = static_cast<float>(glyph->advance.x) / 64.0F,
                .pixels = glyph->bitmap.buffer,
                .pitch = static_cast<U32>(pitch < 0 ? -pitch : pitch),
            };
            return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 1U};
        }
    }

private:
    struct FontSlot {
        FT_Face face;
        hb_font_t* hb_font;
        U32 font_id;
        U32 generation;
        float pixel_size;
        bool occupied;
    };

    static void destroySlot(FontSlot& slot_)
    {
        if (slot_.hb_font != nullptr) {
            hb_font_destroy(slot_.hb_font);
            slot_.hb_font = nullptr;
        }
        if (slot_.face != nullptr) {
            FT_Done_Face(slot_.face);
            slot_.face = nullptr;
        }
        slot_.occupied = false;
    }

    FontSlot& acquireSlot(U32 font_id_)
    {
        for (auto& slot : slots) {
            if (slot.occupied && slot.font_id == font_id_) {
                destroySlot(slot);
                slot.generation += 1U;
                return slot;
            }
        }
        for (auto& slot : slots) {
            if (!slot.occupied) {
                slot.generation += 1U;
                return slot;
            }
        }
        slots.push_back({.face = nullptr, .hb_font = nullptr, .font_id = 0U, .generation = 1U, .pixel_size = 0.0F, .occupied = false});
        return slots[slots.size() - 1U];
    }

    const FontSlot* findSlot(U32 font_id_) const noexcept
    {
        for (const auto& slot : slots) {
            if (slot.occupied && slot.font_id == font_id_) {
                return &slot;
            }
        }
        return nullptr;
    }

    FontSlot* findSlotMutable(U32 font_id_) noexcept
    {
        for (auto& slot : slots) {
            if (slot.occupied && slot.font_id == font_id_) {
                return &slot;
            }
        }
        return nullptr;
    }

    FT_Library library = nullptr;
    hb_buffer_t* shape_buffer = nullptr;
    McVector<FontSlot> slots;
    McVector<std::uint32_t> codepoint_scratch;
};

} // namespace Render2D
