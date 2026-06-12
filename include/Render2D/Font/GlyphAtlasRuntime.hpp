#pragma once

// Stage 19F: glyph rasterization + atlas residency.
//
// Part of the font/text runtime; links FreeType (via FontShapeRuntime) and the
// Stage 18 Vulkan atlas/resource runtimes. NOT included by the umbrella
// `Render2D.hpp`; consumers link `render2d_font_runtime_support`.
//
// Owns one R8_UNORM glyph atlas image (a Stage 18 VulkanTextureAtlasRuntime
// atlas) plus a host-visible staging buffer and a shelf packer. `ensureResident`
// rasterizes each distinct (font, glyph, size) once, packs its 8-bit coverage
// bitmap into the atlas, records the upload into the caller's command buffer,
// and emits POD GlyphAtlasEntry records (UV + bitmap size + bearings) that the
// pure GlyphPositionSystem consumes. The caller transitions the atlas image to
// TRANSFER_DST_OPTIMAL before ensureResident and to SHADER_READ_ONLY_OPTIMAL
// after, mirroring the Stage 18 upload smoke.

#include "Render2D/Component/Text.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Core/Types.hpp"
#include "Render2D/Font/FontShapeRuntime.hpp"
#include "Render2D/Memory/RenderVector.hpp"
#include "Render2D/Meta/Domain.hpp"
#include "Render2D/Native/VulkanResourceRuntime.hpp"
#include "Render2D/Native/VulkanTextureAtlasRuntime.hpp"

#include <vulkan/vulkan.h>

#include <span>

namespace Render2D {

template<class Provider, class Dim>
struct GlyphAtlasRuntimeConfig {
    VulkanResourceRuntime<Provider, Dim>* resource_runtime;
    VulkanTextureAtlasRuntime<Provider, Dim>* atlas_runtime;
    U32 atlas_width;
    U32 atlas_height;
    U32 padding;
};

template<class Provider, class Dim>
class GlyphAtlasRuntime {
public:
    GlyphAtlasRuntime() = default;
    GlyphAtlasRuntime(const GlyphAtlasRuntime&) = delete;
    GlyphAtlasRuntime& operator=(const GlyphAtlasRuntime&) = delete;
    GlyphAtlasRuntime(GlyphAtlasRuntime&&) = delete;
    GlyphAtlasRuntime& operator=(GlyphAtlasRuntime&&) = delete;
    ~GlyphAtlasRuntime() = default;

    SystemResult initialize(GlyphAtlasRuntimeConfig<Provider, Dim> config_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (config_.resource_runtime == nullptr || config_.atlas_runtime == nullptr ||
                config_.atlas_width == 0U || config_.atlas_height == 0U || initialized) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }

            resource_runtime = config_.resource_runtime;
            atlas_runtime = config_.atlas_runtime;
            atlas_width = config_.atlas_width;
            atlas_height = config_.atlas_height;
            padding = config_.padding;

            const NativeResult atlas_result = atlas_runtime->createAtlasImage(
                {.width = atlas_width, .height = atlas_height, .format = static_cast<U32>(VK_FORMAT_R8_UNORM), .flags = 0U},
                atlas_ref);
            if (atlas_result.code != NativeStatusCode::Ok) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            const NativeResult resolve_result = atlas_runtime->resolveImageRef(atlas_ref, atlas_texture);
            if (resolve_result.code != NativeStatusCode::Ok) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }

            staging_capacity = static_cast<U64>(atlas_width) * static_cast<U64>(atlas_height);
            const NativeResult buffer_result = resource_runtime->createBufferRef(
                staging_capacity,
                static_cast<U32>(VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
                NativeMemoryDomain::Upload,
                staging_buffer);
            if (buffer_result.code != NativeStatusCode::Ok) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }

            cursor_x = 0U;
            cursor_y = 0U;
            row_height = 0U;
            initialized = true;
            return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 0U};
        }
    }

    void shutdown()
    {
        if (!initialized) {
            return;
        }
        atlas_runtime->releaseAtlasImage(atlas_ref);
        resource_runtime->releaseBufferRef(staging_buffer);
        cache.clear();
        initialized = false;
    }

    [[nodiscard]] const VulkanAtlasImageRef& atlasImageRef() const noexcept { return atlas_ref; }
    [[nodiscard]] const ImageRef<Provider, Dim>& atlasTexture() const noexcept { return atlas_texture; }

    // Makes every distinct glyph referenced by shaped_glyphs_ resident in the
    // atlas (rasterizing + uploading cache misses into command_buffer_) and
    // emits one GlyphAtlasEntry per distinct (font, glyph, size). The atlas
    // image must be in TRANSFER_DST_OPTIMAL while this records.
    SystemResult ensureResident(
        std::span<const ShapedGlyph<Provider, Dim>> shaped_glyphs_,
        std::span<const ShapingRun<Provider, Dim>> runs_,
        std::span<const Text<Provider, Dim>> texts_,
        FontShapeRuntime<Provider, Dim>& shape_runtime_,
        VkCommandBuffer command_buffer_,
        std::span<GlyphAtlasEntry<Provider, Dim>> out_entries_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!initialized ||
                !isSystemResultCountRepresentable(shaped_glyphs_.size()) ||
                !isSystemResultCountRepresentable(out_entries_.size())) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }

            U64 staging_offset = 0U;
            U32 write_count = 0U;
            const U32 glyph_count = static_cast<U32>(shaped_glyphs_.size());
            for (U32 index = 0U; index < glyph_count; ++index) {
                const auto& shaped = shaped_glyphs_[index];
                if (shaped.run_index >= runs_.size() || runs_[shaped.run_index].text_index >= texts_.size()) {
                    return {.code = SystemStatusCode::InvalidInput, .read_count = index, .write_count = write_count};
                }
                const U32 font_id = runs_[shaped.run_index].font_id;
                const float pixel_size = texts_[runs_[shaped.run_index].text_index].pixel_size;

                if (containsEntry(out_entries_, write_count, font_id, shaped.glyph_id, pixel_size)) {
                    continue;
                }

                GlyphAtlasEntry<Provider, Dim> entry{};
                if (!findCached(font_id, shaped.glyph_id, pixel_size, entry)) {
                    const SystemStatusCode code = makeResident(
                        font_id, shaped.glyph_id, pixel_size, shape_runtime_, command_buffer_, staging_offset, entry);
                    if (code != SystemStatusCode::Ok) {
                        return {.code = code, .read_count = index, .write_count = write_count};
                    }
                    cache.push_back(entry);
                }

                if (static_cast<Usize>(write_count) >= out_entries_.size()) {
                    return {.code = SystemStatusCode::InsufficientCapacity, .read_count = index, .write_count = write_count};
                }
                out_entries_[write_count] = entry;
                ++write_count;
            }

            return {.code = SystemStatusCode::Ok, .read_count = glyph_count, .write_count = write_count};
        }
    }

private:
    static bool containsEntry(
        std::span<const GlyphAtlasEntry<Provider, Dim>> entries_,
        U32 count_,
        U32 font_id_,
        U32 glyph_id_,
        float pixel_size_) noexcept
    {
        for (U32 index = 0U; index < count_; ++index) {
            if (entries_[index].font_id == font_id_ &&
                entries_[index].glyph_id == glyph_id_ &&
                entries_[index].pixel_size == pixel_size_) {
                return true;
            }
        }
        return false;
    }

    bool findCached(U32 font_id_, U32 glyph_id_, float pixel_size_, GlyphAtlasEntry<Provider, Dim>& out_) const noexcept
    {
        for (const auto& entry : cache) {
            if (entry.font_id == font_id_ && entry.glyph_id == glyph_id_ && entry.pixel_size == pixel_size_) {
                out_ = entry;
                return true;
            }
        }
        return false;
    }

    SystemStatusCode makeResident(
        U32 font_id_,
        U32 glyph_id_,
        float pixel_size_,
        FontShapeRuntime<Provider, Dim>& shape_runtime_,
        VkCommandBuffer command_buffer_,
        U64& staging_offset_,
        GlyphAtlasEntry<Provider, Dim>& out_entry_)
    {
        FontGlyphBitmap bitmap{};
        const SystemResult raster = shape_runtime_.rasterizeGlyph(font_id_, glyph_id_, bitmap);
        if (raster.code != SystemStatusCode::Ok) {
            return SystemStatusCode::InvalidInput;
        }

        out_entry_ = {
            .font_id = font_id_,
            .generation = 0U,
            .glyph_id = glyph_id_,
            .pixel_size = pixel_size_,
            .atlas_id = atlas_texture.image_id,
            .atlas_generation = atlas_texture.generation,
            .atlas_rect = makeAabb2(0.0F, 0.0F, 0.0F, 0.0F),
            .bitmap_width = static_cast<float>(bitmap.width),
            .bitmap_height = static_cast<float>(bitmap.height),
            .bearing_x = static_cast<float>(bitmap.bearing_x),
            .bearing_y = static_cast<float>(bitmap.bearing_y),
            .flags = 0U,
        };

        // Whitespace (zero-size bitmap): resident with an empty rect, no upload.
        if (bitmap.width == 0U || bitmap.height == 0U || bitmap.pixels == nullptr) {
            return SystemStatusCode::Ok;
        }

        U32 place_x = 0U;
        U32 place_y = 0U;
        if (!packGlyph(bitmap.width, bitmap.height, place_x, place_y)) {
            return SystemStatusCode::InsufficientCapacity;
        }

        const U64 glyph_bytes = static_cast<U64>(bitmap.width) * static_cast<U64>(bitmap.height);
        if (staging_offset_ + glyph_bytes > staging_capacity) {
            return SystemStatusCode::InsufficientCapacity;
        }
        row_scratch.resize(static_cast<Usize>(glyph_bytes));
        for (U32 row = 0U; row < bitmap.height; ++row) {
            for (U32 col = 0U; col < bitmap.width; ++col) {
                row_scratch[static_cast<Usize>(row) * bitmap.width + col] = bitmap.pixels[static_cast<Usize>(row) * bitmap.pitch + col];
            }
        }
        const NativeResult write_result = resource_runtime->writeBuffer(
            staging_buffer, row_scratch.data(), glyph_bytes, staging_offset_);
        if (write_result.code != NativeStatusCode::Ok) {
            return SystemStatusCode::InvalidInput;
        }
        const NativeResult upload_result = atlas_runtime->recordUploadRegion(
            command_buffer_, atlas_ref, staging_buffer, staging_offset_, place_x, place_y, bitmap.width, bitmap.height);
        if (upload_result.code != NativeStatusCode::Ok) {
            return SystemStatusCode::InvalidInput;
        }
        staging_offset_ += glyph_bytes;

        const float inv_width = 1.0F / static_cast<float>(atlas_width);
        const float inv_height = 1.0F / static_cast<float>(atlas_height);
        out_entry_.atlas_rect = makeAabb2(
            static_cast<float>(place_x) * inv_width,
            static_cast<float>(place_y) * inv_height,
            static_cast<float>(place_x + bitmap.width) * inv_width,
            static_cast<float>(place_y + bitmap.height) * inv_height);
        return SystemStatusCode::Ok;
    }

    bool packGlyph(U32 width_, U32 height_, U32& out_x_, U32& out_y_) noexcept
    {
        if (width_ > atlas_width || height_ > atlas_height) {
            return false;
        }
        if (cursor_x + width_ > atlas_width) {
            cursor_x = 0U;
            cursor_y += row_height + padding;
            row_height = 0U;
        }
        if (cursor_y + height_ > atlas_height) {
            return false;
        }
        out_x_ = cursor_x;
        out_y_ = cursor_y;
        cursor_x += width_ + padding;
        row_height = row_height < height_ ? height_ : row_height;
        return true;
    }

    VulkanResourceRuntime<Provider, Dim>* resource_runtime = nullptr;
    VulkanTextureAtlasRuntime<Provider, Dim>* atlas_runtime = nullptr;
    VulkanAtlasImageRef atlas_ref{};
    ImageRef<Provider, Dim> atlas_texture{};
    BufferRef<Provider, Dim> staging_buffer{};
    U64 staging_capacity = 0U;
    U32 atlas_width = 0U;
    U32 atlas_height = 0U;
    U32 padding = 0U;
    U32 cursor_x = 0U;
    U32 cursor_y = 0U;
    U32 row_height = 0U;
    bool initialized = false;
    McVector<GlyphAtlasEntry<Provider, Dim>> cache;
    McVector<U8> row_scratch;
};

} // namespace Render2D
