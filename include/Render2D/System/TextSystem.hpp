#pragma once

#include "Render2D/Component/Command.hpp"
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

struct GlyphDrawConfig {
    U32 material_id;
    U32 vertex_first;
    U32 vertex_count;
    U32 index_first;
    U32 index_count;
    U32 flags;
};

inline constexpr GlyphDrawConfig kDefaultGlyphDrawConfig{
    .material_id = 0U,
    .vertex_first = 0U,
    .vertex_count = 4U,
    .index_first = 0U,
    .index_count = 6U,
    .flags = 0U,
};

inline constexpr U32 kTextDirtyCreatedFlag = 1U << 0U;
inline constexpr U32 kTextDirtyContentChangedFlag = 1U << 1U;
inline constexpr U32 kTextDirtyStyleChangedFlag = 1U << 2U;
inline constexpr U32 kTextDirtyGlyphRangeChangedFlag = 1U << 3U;

template<class Provider, class Dim>
struct TextDirtySystem {
    static SystemResult run(
        std::span<const Text<Provider, Dim>> texts_,
        std::span<const TextState<Provider, Dim>> previous_states_,
        std::span<TextState<Provider, Dim>> next_states_,
        std::span<TextDirtyRange<Provider, Dim>> dirty_ranges_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (texts_.size() > (std::numeric_limits<U32>::max)()) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            if (!previous_states_.empty() && previous_states_.size() != texts_.size()) {
                return {
                    .code = SystemStatusCode::InvalidInput,
                    .read_count = static_cast<U32>(texts_.size()),
                    .write_count = 0U,
                };
            }
            if (next_states_.size() < texts_.size()) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = static_cast<U32>(texts_.size()),
                    .write_count = static_cast<U32>(next_states_.size()),
                };
            }

            U32 glyph_cursor = 0U;
            U32 dirty_count = 0U;
            for (Usize index = 0U; index < texts_.size(); ++index) {
                const auto& text = texts_[index];
                if (text.utf8_size > (std::numeric_limits<U32>::max)() - glyph_cursor) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(index),
                        .write_count = dirty_count,
                    };
                }

                const auto next_state = makeState(text, glyph_cursor);
                next_states_[index] = next_state;

                const auto* previous_state = previous_states_.empty() ? nullptr : &previous_states_[index];
                const U32 dirty_flags = classifyDirty(text, previous_state, glyph_cursor);
                if (dirty_flags != 0U) {
                    if (dirty_count >= dirty_ranges_.size()) {
                        return {
                            .code = SystemStatusCode::InsufficientCapacity,
                            .read_count = static_cast<U32>(index),
                            .write_count = dirty_count,
                        };
                    }
                    dirty_ranges_[dirty_count] = {
                        .source_text_index = static_cast<U32>(index),
                        .previous_glyph_first = previous_state == nullptr ? 0U : previous_state->glyph_first,
                        .previous_glyph_count = previous_state == nullptr ? 0U : previous_state->glyph_count,
                        .new_glyph_first = glyph_cursor,
                        .new_glyph_count = text.utf8_size,
                        .flags = dirty_flags,
                    };
                    ++dirty_count;
                }

                glyph_cursor += text.utf8_size;
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(texts_.size()),
                .write_count = dirty_count,
            };
        }
    }

private:
    static TextState<Provider, Dim> makeState(
        const Text<Provider, Dim>& text_,
        U32 glyph_first_) noexcept
    {
        return {
            .source_id = text_.source_id,
            .font_id = text_.font_id,
            .utf8_buffer_id = text_.utf8_buffer_id,
            .utf8_offset = text_.utf8_offset,
            .utf8_size = text_.utf8_size,
            .color_rgba8 = text_.color_rgba8,
            .pixel_size = text_.pixel_size,
            .layer = text_.layer,
            .flags = text_.flags,
            .glyph_first = glyph_first_,
            .glyph_count = text_.utf8_size,
        };
    }

    static U32 classifyDirty(
        const Text<Provider, Dim>& text_,
        const TextState<Provider, Dim>* previous_state_,
        U32 glyph_first_) noexcept
    {
        if (previous_state_ == nullptr) {
            return kTextDirtyCreatedFlag;
        }

        U32 flags = 0U;
        if (previous_state_->source_id != text_.source_id ||
            previous_state_->font_id != text_.font_id ||
            previous_state_->utf8_buffer_id != text_.utf8_buffer_id ||
            previous_state_->utf8_offset != text_.utf8_offset ||
            previous_state_->utf8_size != text_.utf8_size) {
            flags |= kTextDirtyContentChangedFlag;
        }
        if (previous_state_->color_rgba8 != text_.color_rgba8 ||
            previous_state_->pixel_size != text_.pixel_size ||
            previous_state_->layer != text_.layer ||
            previous_state_->flags != text_.flags) {
            flags |= kTextDirtyStyleChangedFlag;
        }
        if (previous_state_->glyph_first != glyph_first_ ||
            previous_state_->glyph_count != text_.utf8_size) {
            flags |= kTextDirtyGlyphRangeChangedFlag;
        }
        return flags;
    }
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

    static SystemResult runDirty(
        std::span<const Text<Provider, Dim>> texts_,
        std::span<const TextDirtyRange<Provider, Dim>> dirty_ranges_,
        std::span<const FontAtlasRef<Provider, Dim>> font_atlases_,
        std::span<GlyphRun<Provider, Dim>> glyph_runs_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (dirty_ranges_.size() > (std::numeric_limits<U32>::max)()) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }

            for (Usize index = 0U; index < dirty_ranges_.size(); ++index) {
                const auto& range = dirty_ranges_[index];
                if (range.source_text_index >= texts_.size() || range.source_text_index >= glyph_runs_.size()) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(index),
                        .write_count = static_cast<U32>(index),
                    };
                }

                const auto& text = texts_[range.source_text_index];
                if (range.new_glyph_count != text.utf8_size) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(index),
                        .write_count = static_cast<U32>(index),
                    };
                }

                const auto* atlas = findAtlas(text.font_id, font_atlases_);
                if (atlas == nullptr) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(index),
                        .write_count = static_cast<U32>(index),
                    };
                }

                glyph_runs_[range.source_text_index] = {
                    .source_text_index = range.source_text_index,
                    .glyph_first = range.new_glyph_first,
                    .glyph_count = range.new_glyph_count,
                    .atlas_id = atlas->atlas_id,
                    .atlas_generation = atlas->generation,
                    .flags = text.flags,
                };
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(dirty_ranges_.size()),
                .write_count = static_cast<U32>(dirty_ranges_.size()),
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

            U32 required_instance_count = 0U;
            for (Usize run_index = 0U; run_index < glyph_runs_.size(); ++run_index) {
                const auto& run = glyph_runs_[run_index];
                if (run.source_text_index >= texts_.size() ||
                    run.glyph_first > (std::numeric_limits<U32>::max)() - run.glyph_count) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(run_index),
                        .write_count = required_instance_count,
                    };
                }
                if (run.glyph_count == 0U) {
                    continue;
                }
                const U32 run_end = run.glyph_first + run.glyph_count;
                if (required_instance_count < run_end) {
                    required_instance_count = run_end;
                }
            }

            if (glyph_instances_.size() < required_instance_count) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = static_cast<U32>(glyph_runs_.size()),
                    .write_count = static_cast<U32>(glyph_instances_.size()),
                };
            }

            for (Usize run_index = 0U; run_index < glyph_runs_.size(); ++run_index) {
                const auto& run = glyph_runs_[run_index];
                const auto& text = texts_[run.source_text_index];
                for (U32 glyph_index = 0U; glyph_index < run.glyph_count; ++glyph_index) {
                    U32 glyph_id = 0U;
                    if (!makeGlyphId(config_, run.glyph_first, glyph_index, glyph_id)) {
                        return {
                            .code = SystemStatusCode::InvalidInput,
                            .read_count = static_cast<U32>(run_index),
                            .write_count = required_instance_count,
                        };
                    }

                    const U32 write_index = run.glyph_first + glyph_index;
                    glyph_instances_[write_index] = makeGlyphInstance(
                        static_cast<U32>(run_index),
                        glyph_id,
                        glyph_index,
                        text,
                        run,
                        config_);
                }
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(glyph_runs_.size()),
                .write_count = required_instance_count,
            };
        }
    }

    static SystemResult runDirty(
        std::span<const GlyphRun<Provider, Dim>> glyph_runs_,
        std::span<const TextDirtyRange<Provider, Dim>> dirty_ranges_,
        std::span<const Text<Provider, Dim>> texts_,
        GlyphBuildConfig config_,
        std::span<GlyphInstance<Provider, Dim>> glyph_instances_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isValidConfig(config_) || dirty_ranges_.size() > (std::numeric_limits<U32>::max)()) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }

            U32 written_glyph_count = 0U;
            for (Usize range_index = 0U; range_index < dirty_ranges_.size(); ++range_index) {
                const auto& range = dirty_ranges_[range_index];
                if (range.source_text_index >= glyph_runs_.size()) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(range_index),
                        .write_count = written_glyph_count,
                    };
                }

                const auto& run = glyph_runs_[range.source_text_index];
                if (run.source_text_index >= texts_.size() ||
                    run.glyph_first != range.new_glyph_first ||
                    run.glyph_count != range.new_glyph_count ||
                    run.glyph_first > (std::numeric_limits<U32>::max)() - run.glyph_count ||
                    static_cast<Usize>(run.glyph_first + run.glyph_count) > glyph_instances_.size()) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(range_index),
                        .write_count = written_glyph_count,
                    };
                }

                const auto& text = texts_[run.source_text_index];
                for (U32 glyph_index = 0U; glyph_index < run.glyph_count; ++glyph_index) {
                    U32 glyph_id = 0U;
                    if (!makeGlyphId(config_, run.glyph_first, glyph_index, glyph_id)) {
                        return {
                            .code = SystemStatusCode::InvalidInput,
                            .read_count = static_cast<U32>(range_index),
                            .write_count = written_glyph_count,
                        };
                    }

                    const U32 write_index = run.glyph_first + glyph_index;
                    glyph_instances_[write_index] = makeGlyphInstance(
                        range.source_text_index,
                        glyph_id,
                        glyph_index,
                        text,
                        run,
                        config_);
                    ++written_glyph_count;
                }
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(dirty_ranges_.size()),
                .write_count = written_glyph_count,
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

    static GlyphInstance<Provider, Dim> makeGlyphInstance(
        U32 glyph_run_index_,
        U32 glyph_id_,
        U32 glyph_index_,
        const Text<Provider, Dim>& text_,
        const GlyphRun<Provider, Dim>& run_,
        GlyphBuildConfig config_) noexcept
    {
        return {
            .glyph_run_index = glyph_run_index_,
            .glyph_id = glyph_id_,
            .atlas_rect = makeAtlasRect(glyph_id_, config_),
            .position_x = static_cast<float>(glyph_index_) * text_.pixel_size * config_.advance_x_scale,
            .position_y = config_.baseline_y,
            .color_rgba8 = text_.color_rgba8,
            .sort_key = text_.layer,
            .layer = text_.layer,
            .flags = text_.flags | run_.flags,
        };
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

template<class Provider, class Dim>
struct GlyphBatchSystem {
    static SystemResult run(
        std::span<const GlyphRun<Provider, Dim>> glyph_runs_,
        std::span<const Text<Provider, Dim>> texts_,
        std::span<const FontAtlasRef<Provider, Dim>> font_atlases_,
        GlyphDrawConfig config_,
        std::span<DrawCommand<Provider, Dim>> draw_commands_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isValidConfig(config_) || glyph_runs_.size() > (std::numeric_limits<U32>::max)()) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }

            U32 draw_count = 0U;
            for (Usize index = 0U; index < glyph_runs_.size(); ++index) {
                const auto& run = glyph_runs_[index];
                if (run.glyph_count == 0U) {
                    continue;
                }
                if (run.source_text_index >= texts_.size()) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(index),
                        .write_count = draw_count,
                    };
                }
                if (draw_count >= draw_commands_.size()) {
                    return {
                        .code = SystemStatusCode::InsufficientCapacity,
                        .read_count = static_cast<U32>(index),
                        .write_count = draw_count,
                    };
                }

                const auto& text = texts_[run.source_text_index];
                const auto* atlas = findAtlas(text.font_id, run.atlas_id, run.atlas_generation, font_atlases_);
                if (atlas == nullptr) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(index),
                        .write_count = draw_count,
                    };
                }

                draw_commands_[draw_count] = {
                    .source_index = run.source_text_index,
                    .material_id = config_.material_id,
                    .texture_id = atlas->texture_id,
                    .vertex_first = config_.vertex_first,
                    .vertex_count = config_.vertex_count,
                    .index_first = config_.index_first,
                    .index_count = config_.index_count,
                    .instance_first = run.glyph_first,
                    .instance_count = run.glyph_count,
                    .sort_key = text.layer,
                    .layer = text.layer,
                    .flags = text.flags | run.flags | atlas->flags | config_.flags,
                };
                ++draw_count;
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(glyph_runs_.size()),
                .write_count = draw_count,
            };
        }
    }

private:
    static bool isValidConfig(GlyphDrawConfig config_) noexcept
    {
        return config_.vertex_count != 0U && config_.index_count != 0U;
    }

    static const FontAtlasRef<Provider, Dim>* findAtlas(
        U32 font_id_,
        U32 atlas_id_,
        U32 atlas_generation_,
        std::span<const FontAtlasRef<Provider, Dim>> font_atlases_) noexcept
    {
        for (const auto& atlas : font_atlases_) {
            if (atlas.font_id == font_id_ &&
                atlas.atlas_id == atlas_id_ &&
                atlas.generation == atlas_generation_) {
                return &atlas;
            }
        }
        return nullptr;
    }
};

static_assert(std::is_trivial_v<GlyphBuildConfig>);
static_assert(std::is_standard_layout_v<GlyphBuildConfig>);
static_assert(std::is_trivially_copyable_v<GlyphBuildConfig>);
static_assert(std::is_aggregate_v<GlyphBuildConfig>);

static_assert(std::is_trivial_v<GlyphDrawConfig>);
static_assert(std::is_standard_layout_v<GlyphDrawConfig>);
static_assert(std::is_trivially_copyable_v<GlyphDrawConfig>);
static_assert(std::is_aggregate_v<GlyphDrawConfig>);

} // namespace Render2D
