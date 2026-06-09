#pragma once

#include "Render2D/Component/Text.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Meta/Domain.hpp"

#include <limits>
#include <span>
#include <type_traits>

namespace Render2D {

struct GlyphBuildConfig {
    float advance_x_scale;
    float baseline_y;
    U32 glyph_id_first;
    U32 atlas_column_count;
    float atlas_cell_width;
    float atlas_cell_height;
};

inline constexpr GlyphBuildConfig kDefaultGlyphBuildConfig{
    .advance_x_scale = 0.5F,
    .baseline_y = 0.0F,
    .glyph_id_first = 0U,
    .atlas_column_count = 16U,
    .atlas_cell_width = 1.0F / 16.0F,
    .atlas_cell_height = 1.0F / 16.0F,
};

template<class Provider, class Dim>
struct GlyphRunBuildSystem {
    static SystemResult run(
        std::span<const Text<Provider, Dim>> texts_,
        std::span<const FontAtlasRef<Provider, Dim>> font_atlases_,
        std::span<GlyphRun<Provider, Dim>> glyph_runs_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (texts_.size() > (std::numeric_limits<U32>::max)()) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            if (glyph_runs_.size() < texts_.size()) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = static_cast<U32>(texts_.size()),
                    .write_count = static_cast<U32>(glyph_runs_.size()),
                };
            }

            U32 glyph_cursor = 0U;
            for (Usize index = 0U; index < texts_.size(); ++index) {
                const auto& text = texts_[index];
                const auto* atlas = findAtlas(text.font_id, font_atlases_);
                if (atlas == nullptr) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(index),
                        .write_count = static_cast<U32>(index),
                    };
                }
                if (text.utf8_size > (std::numeric_limits<U32>::max)() - glyph_cursor) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(index),
                        .write_count = static_cast<U32>(index),
                    };
                }

                glyph_runs_[index] = {
                    .source_text_index = static_cast<U32>(index),
                    .glyph_first = glyph_cursor,
                    .glyph_count = text.utf8_size,
                    .atlas_id = atlas->atlas_id,
                    .atlas_generation = atlas->generation,
                    .flags = text.flags,
                };
                glyph_cursor += text.utf8_size;
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(texts_.size()),
                .write_count = static_cast<U32>(texts_.size()),
            };
        }
    }

private:
    static const FontAtlasRef<Provider, Dim>* findAtlas(
        U32 font_id_,
        std::span<const FontAtlasRef<Provider, Dim>> font_atlases_) noexcept
    {
        for (const auto& atlas : font_atlases_) {
            if (atlas.font_id == font_id_) {
                return &atlas;
            }
        }
        return nullptr;
    }
};

template<class Provider, class Dim>
struct GlyphInstanceBuildSystem {
    static SystemResult run(
        std::span<const GlyphRun<Provider, Dim>> glyph_runs_,
        std::span<const Text<Provider, Dim>> texts_,
        GlyphBuildConfig config_,
        std::span<GlyphInstance<Provider, Dim>> glyph_instances_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isValidConfig(config_)) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }

            U32 total_glyph_count = 0U;
            for (Usize run_index = 0U; run_index < glyph_runs_.size(); ++run_index) {
                const auto& run = glyph_runs_[run_index];
                if (run.source_text_index >= texts_.size() ||
                    run.glyph_count > (std::numeric_limits<U32>::max)() - total_glyph_count) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(run_index),
                        .write_count = total_glyph_count,
                    };
                }
                total_glyph_count += run.glyph_count;
            }

            if (glyph_instances_.size() < total_glyph_count) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = static_cast<U32>(glyph_runs_.size()),
                    .write_count = static_cast<U32>(glyph_instances_.size()),
                };
            }

            U32 write_index = 0U;
            for (Usize run_index = 0U; run_index < glyph_runs_.size(); ++run_index) {
                const auto& run = glyph_runs_[run_index];
                const auto& text = texts_[run.source_text_index];
                for (U32 glyph_index = 0U; glyph_index < run.glyph_count; ++glyph_index) {
                    U32 glyph_id = 0U;
                    if (!makeGlyphId(config_, run.glyph_first, glyph_index, glyph_id)) {
                        return {
                            .code = SystemStatusCode::InvalidInput,
                            .read_count = static_cast<U32>(run_index),
                            .write_count = write_index,
                        };
                    }

                    glyph_instances_[write_index] = {
                        .glyph_run_index = static_cast<U32>(run_index),
                        .glyph_id = glyph_id,
                        .atlas_rect = makeAtlasRect(glyph_id, config_),
                        .position_x = static_cast<float>(glyph_index) * text.pixel_size * config_.advance_x_scale,
                        .position_y = config_.baseline_y,
                        .color_rgba8 = text.color_rgba8,
                        .sort_key = text.layer,
                        .layer = text.layer,
                        .flags = text.flags | run.flags,
                    };
                    ++write_index;
                }
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(glyph_runs_.size()),
                .write_count = write_index,
            };
        }
    }

private:
    static bool isValidConfig(GlyphBuildConfig config_) noexcept
    {
        return config_.advance_x_scale > 0.0F &&
            config_.atlas_column_count != 0U &&
            config_.atlas_cell_width > 0.0F &&
            config_.atlas_cell_height > 0.0F;
    }

    static bool makeGlyphId(
        GlyphBuildConfig config_,
        U32 glyph_first_,
        U32 glyph_index_,
        U32& out_glyph_id_) noexcept
    {
        if (glyph_first_ > (std::numeric_limits<U32>::max)() - glyph_index_) {
            return false;
        }
        const U32 glyph_offset = glyph_first_ + glyph_index_;
        if (config_.glyph_id_first > (std::numeric_limits<U32>::max)() - glyph_offset) {
            return false;
        }
        out_glyph_id_ = config_.glyph_id_first + glyph_offset;
        return true;
    }

    static Aabb2 makeAtlasRect(U32 glyph_id_, GlyphBuildConfig config_) noexcept
    {
        const U32 column = glyph_id_ % config_.atlas_column_count;
        const U32 row = glyph_id_ / config_.atlas_column_count;
        const float min_x = static_cast<float>(column) * config_.atlas_cell_width;
        const float min_y = static_cast<float>(row) * config_.atlas_cell_height;
        return {
            .min_x = min_x,
            .min_y = min_y,
            .max_x = min_x + config_.atlas_cell_width,
            .max_y = min_y + config_.atlas_cell_height,
        };
    }
};

static_assert(std::is_trivial_v<GlyphBuildConfig>);
static_assert(std::is_standard_layout_v<GlyphBuildConfig>);
static_assert(std::is_trivially_copyable_v<GlyphBuildConfig>);
static_assert(std::is_aggregate_v<GlyphBuildConfig>);

} // namespace Render2D
