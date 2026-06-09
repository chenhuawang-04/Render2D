#include <Render2D/Render2D.hpp>

#include <array>
#include <cassert>
#include <span>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Text = R2D::Text<Provider, Dim>;
using FontAtlasRef = R2D::FontAtlasRef<Provider, Dim>;
using GlyphRun = R2D::GlyphRun<Provider, Dim>;
using GlyphInstance = R2D::GlyphInstance<Provider, Dim>;

static_assert(R2D::SupportedRenderComponent<Provider, Dim, Text>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, FontAtlasRef>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, GlyphRun>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, GlyphInstance>);

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
            .layer = 4U,
            .flags = 1U,
        },
        {
            .source_id = 1U,
            .font_id = 3U,
            .utf8_buffer_id = 1U,
            .utf8_offset = 2U,
            .utf8_size = 1U,
            .color_rgba8 = 0xFFFFFFFFU,
            .pixel_size = 10.0F,
            .layer = 5U,
            .flags = 2U,
        },
    }};

    constexpr std::array<FontAtlasRef, 1U> kAtlases{{
        {
            .font_id = 3U,
            .atlas_id = 9U,
            .generation = 7U,
            .texture_id = 11U,
            .flags = 0U,
        },
    }};

    std::array<GlyphRun, kTexts.size()> glyph_runs{};
    auto result = R2D::GlyphRunBuildSystem<Provider, Dim>::run(kTexts, kAtlases, glyph_runs);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.read_count == kTexts.size());
    assert(result.write_count == kTexts.size());
    assert(glyph_runs[0U].source_text_index == 0U);
    assert(glyph_runs[0U].glyph_first == 0U);
    assert(glyph_runs[0U].glyph_count == 2U);
    assert(glyph_runs[1U].glyph_first == 2U);
    assert(glyph_runs[1U].glyph_count == 1U);
    assert(glyph_runs[1U].atlas_id == 9U);
    assert(glyph_runs[1U].atlas_generation == 7U);

    constexpr R2D::GlyphBuildConfig kConfig{
        .advance_x_scale = 0.5F,
        .baseline_y = 3.0F,
        .glyph_id_first = 32U,
        .atlas_column_count = 16U,
        .atlas_cell_width = 1.0F / 16.0F,
        .atlas_cell_height = 1.0F / 16.0F,
    };

    std::array<GlyphInstance, 3U> glyph_instances{};
    result = R2D::GlyphInstanceBuildSystem<Provider, Dim>::run(
        glyph_runs,
        kTexts,
        kConfig,
        glyph_instances);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.read_count == glyph_runs.size());
    assert(result.write_count == glyph_instances.size());
    assert(glyph_instances[0U].glyph_run_index == 0U);
    assert(glyph_instances[0U].glyph_id == 32U);
    assert(glyph_instances[0U].position_x == 0.0F);
    assert(glyph_instances[0U].position_y == 3.0F);
    assert(R2D::aabb2Min(glyph_instances[0U].atlas_rect).x == 0.0F);
    assert(R2D::aabb2Max(glyph_instances[0U].atlas_rect).x == 1.0F / 16.0F);
    assert(glyph_instances[0U].color_rgba8 == kTexts[0U].color_rgba8);
    assert(glyph_instances[1U].glyph_id == 33U);
    assert(glyph_instances[1U].position_x == 10.0F);
    assert(glyph_instances[2U].glyph_run_index == 1U);
    assert(glyph_instances[2U].glyph_id == 34U);
    assert(glyph_instances[2U].layer == 5U);

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
        kConfig,
        offset_instances);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.read_count == kOffsetRuns.size());
    assert(result.write_count == offset_instances.size());
    assert(offset_instances[4U].glyph_run_index == 0U);
    assert(offset_instances[4U].glyph_id == 36U);
    assert(offset_instances[5U].glyph_id == 37U);
    assert(offset_instances[5U].position_x == 10.0F);

    std::array<GlyphInstance, 5U> short_offset_instances{};
    result = R2D::GlyphInstanceBuildSystem<Provider, Dim>::run(
        kOffsetRuns,
        kTexts,
        kConfig,
        short_offset_instances);
    assert(result.code == R2D::SystemStatusCode::InsufficientCapacity);

    std::array<GlyphRun, 1U> short_runs{};
    result = R2D::GlyphRunBuildSystem<Provider, Dim>::run(kTexts, kAtlases, short_runs);
    assert(result.code == R2D::SystemStatusCode::InsufficientCapacity);

    constexpr std::array<FontAtlasRef, 1U> kWrongAtlases{{
        {
            .font_id = 99U,
            .atlas_id = 9U,
            .generation = 7U,
            .texture_id = 11U,
            .flags = 0U,
        },
    }};
    result = R2D::GlyphRunBuildSystem<Provider, Dim>::run(kTexts, kWrongAtlases, glyph_runs);
    assert(result.code == R2D::SystemStatusCode::InvalidInput);

    std::array<GlyphInstance, 2U> short_instances{};
    result = R2D::GlyphInstanceBuildSystem<Provider, Dim>::run(
        glyph_runs,
        kTexts,
        kConfig,
        short_instances);
    assert(result.code == R2D::SystemStatusCode::InsufficientCapacity);

    return 0;
}
