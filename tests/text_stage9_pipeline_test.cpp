#include <Render2D/Render2D.hpp>

#include <array>
#include <cassert>
#include <span>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Text = R2D::Text<Provider, Dim>;
using TextState = R2D::TextState<Provider, Dim>;
using TextDirtyRange = R2D::TextDirtyRange<Provider, Dim>;
using FontAtlasRef = R2D::FontAtlasRef<Provider, Dim>;
using GlyphRun = R2D::GlyphRun<Provider, Dim>;
using GlyphInstance = R2D::GlyphInstance<Provider, Dim>;
using DrawCommand = R2D::DrawCommand<Provider, Dim>;
using BatchCommand = R2D::BatchCommand<Provider, Dim>;

static_assert(R2D::SupportedRenderComponent<Provider, Dim, TextState>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, TextDirtyRange>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, DrawCommand>);

int main()
{
    constexpr std::array<Text, 2U> kTexts{{
        {
            .source_id = 0U,
            .font_id = 3U,
            .utf8_buffer_id = 1U,
            .utf8_offset = 0U,
            .utf8_size = 2U,
            .color_rgba8 = 0xFF00FFFFU,
            .pixel_size = 20.0F,
            .layer = 2U,
            .flags = 0U,
        },
        {
            .source_id = 1U,
            .font_id = 3U,
            .utf8_buffer_id = 1U,
            .utf8_offset = 2U,
            .utf8_size = 1U,
            .color_rgba8 = 0xFFFFFFFFU,
            .pixel_size = 10.0F,
            .layer = 2U,
            .flags = 0U,
        },
    }};

    constexpr std::array<FontAtlasRef, 1U> kAtlases{{
        {
            .font_id = 3U,
            .atlas_id = 9U,
            .generation = 7U,
            .texture_id = 11U,
            .texture_generation = 0U,
            .flags = 0U,
        },
    }};

    std::array<TextState, kTexts.size()> first_states{};
    std::array<TextDirtyRange, kTexts.size()> dirty_ranges{};
    auto result = R2D::TextDirtySystem<Provider, Dim>::run(
        kTexts,
        std::span<const TextState>{},
        first_states,
        dirty_ranges);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.write_count == kTexts.size());
    assert((dirty_ranges[0U].flags & R2D::kTextDirtyCreatedFlag) != 0U);
    assert(dirty_ranges[0U].new_glyph_first == 0U);
    assert(dirty_ranges[1U].new_glyph_first == 2U);

    std::array<GlyphRun, kTexts.size()> glyph_runs{};
    result = R2D::GlyphRunBuildSystem<Provider, Dim>::runDirty(
        kTexts,
        std::span<const TextDirtyRange>{dirty_ranges.data(), result.write_count},
        kAtlases,
        glyph_runs);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.write_count == kTexts.size());
    assert(glyph_runs[0U].glyph_count == 2U);
    assert(glyph_runs[1U].glyph_first == 2U);

    constexpr R2D::GlyphBuildConfig kGlyphConfig{
        .advance_x_scale = 0.5F,
        .baseline_y = 1.0F,
        .glyph_id_first = 32U,
        .atlas_column_count = 16U,
        .atlas_cell_width = 1.0F / 16.0F,
        .atlas_cell_height = 1.0F / 16.0F,
    };

    std::array<GlyphInstance, 3U> glyph_instances{};
    result = R2D::GlyphInstanceBuildSystem<Provider, Dim>::runDirty(
        glyph_runs,
        std::span<const TextDirtyRange>{dirty_ranges.data(), dirty_ranges.size()},
        kTexts,
        kGlyphConfig,
        glyph_instances);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.write_count == glyph_instances.size());
    assert(glyph_instances[0U].glyph_id == 32U);
    assert(glyph_instances[1U].position_x == 10.0F);
    assert(glyph_instances[2U].glyph_run_index == 1U);

    constexpr R2D::GlyphDrawConfig kDrawConfig{
        .material_id = 77U,
        .material_generation = 0U,
        .vertex_first = 0U,
        .vertex_count = 4U,
        .index_first = 0U,
        .index_count = 6U,
        .flags = 0U,
    };

    std::array<DrawCommand, kTexts.size()> glyph_draws{};
    result = R2D::GlyphBatchSystem<Provider, Dim>::run(
        glyph_runs,
        kTexts,
        kAtlases,
        kDrawConfig,
        glyph_draws);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.write_count == kTexts.size());
    assert(glyph_draws[0U].texture_id == 11U);
    assert(glyph_draws[0U].instance_first == 0U);
    assert(glyph_draws[0U].instance_count == 2U);
    assert(glyph_draws[1U].instance_first == 2U);
    assert(glyph_draws[1U].material_id == 77U);

    std::array<BatchCommand, kTexts.size()> batches{};
    result = R2D::BatchSystem<Provider, Dim>::run(glyph_draws, batches);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.write_count == 1U);
    assert(batches[0U].draw_count == 2U);
    assert(batches[0U].texture_id == 11U);

    constexpr std::array<GlyphRun, 1U> kOffsetRuns{{
        {
            .source_text_index = 0U,
            .glyph_first = 4U,
            .glyph_count = 2U,
            .atlas_id = 9U,
            .atlas_generation = 7U,
            .flags = 0U,
        },
    }};
    std::array<GlyphInstance, 6U> offset_instances{};
    result = R2D::GlyphInstanceBuildSystem<Provider, Dim>::run(
        kOffsetRuns,
        kTexts,
        kGlyphConfig,
        offset_instances);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.write_count == offset_instances.size());

    std::array<DrawCommand, 1U> offset_draws{};
    result = R2D::GlyphBatchSystem<Provider, Dim>::run(
        kOffsetRuns,
        kTexts,
        kAtlases,
        kDrawConfig,
        offset_draws);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.write_count == 1U);
    assert(offset_draws[0U].instance_first == 4U);
    assert(offset_draws[0U].instance_count == 2U);
    assert(offset_instances[offset_draws[0U].instance_first].glyph_id == 36U);

    std::array<TextState, kTexts.size()> second_states{};
    std::array<TextDirtyRange, kTexts.size()> clean_ranges{};
    result = R2D::TextDirtySystem<Provider, Dim>::run(
        kTexts,
        first_states,
        second_states,
        clean_ranges);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.write_count == 0U);
    assert(second_states[1U].glyph_first == 2U);

    auto recolored_texts = kTexts;
    recolored_texts[1U].color_rgba8 = 0x00FFFFFFU;
    std::array<TextState, kTexts.size()> third_states{};
    std::array<TextDirtyRange, kTexts.size()> recolor_ranges{};
    result = R2D::TextDirtySystem<Provider, Dim>::run(
        recolored_texts,
        second_states,
        third_states,
        recolor_ranges);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.write_count == 1U);
    assert(recolor_ranges[0U].source_text_index == 1U);
    assert((recolor_ranges[0U].flags & R2D::kTextDirtyStyleChangedFlag) != 0U);

    result = R2D::GlyphInstanceBuildSystem<Provider, Dim>::runDirty(
        glyph_runs,
        std::span<const TextDirtyRange>{recolor_ranges.data(), 1U},
        recolored_texts,
        kGlyphConfig,
        glyph_instances);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.write_count == 1U);
    assert(glyph_instances[2U].color_rgba8 == recolored_texts[1U].color_rgba8);

    auto resized_texts = kTexts;
    resized_texts[0U].utf8_size = 1U;
    std::array<TextState, kTexts.size()> resized_states{};
    std::array<TextDirtyRange, kTexts.size()> resized_ranges{};
    result = R2D::TextDirtySystem<Provider, Dim>::run(
        resized_texts,
        first_states,
        resized_states,
        resized_ranges);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.write_count == 2U);
    assert(resized_ranges[0U].source_text_index == 0U);
    assert((resized_ranges[0U].flags & R2D::kTextDirtyContentChangedFlag) != 0U);
    assert(resized_ranges[1U].source_text_index == 1U);
    assert((resized_ranges[1U].flags & R2D::kTextDirtyGlyphRangeChangedFlag) != 0U);
    assert(resized_ranges[1U].new_glyph_first == 1U);

    std::array<TextDirtyRange, 1U> short_dirty_ranges{};
    result = R2D::TextDirtySystem<Provider, Dim>::run(
        kTexts,
        std::span<const TextState>{},
        first_states,
        short_dirty_ranges);
    assert(result.code == R2D::SystemStatusCode::InsufficientCapacity);

    std::array<DrawCommand, 1U> short_draws{};
    result = R2D::GlyphBatchSystem<Provider, Dim>::run(
        glyph_runs,
        kTexts,
        kAtlases,
        kDrawConfig,
        short_draws);
    assert(result.code == R2D::SystemStatusCode::InsufficientCapacity);

    return 0;
}
