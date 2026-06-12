#include "support/TemporaryEcsStorage.hpp"

#include <Render2D/Render2D.hpp>

#include <cassert>
#include <type_traits>

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Text = R2D::Text<Provider, Dim>;
using TextState = R2D::TextState<Provider, Dim>;
using TextDirtyRange = R2D::TextDirtyRange<Provider, Dim>;
using Utf8Slice = R2D::Utf8Slice<Provider, Dim>;
using FontRef = R2D::FontRef<Provider, Dim>;
using FontAtlasRef = R2D::FontAtlasRef<Provider, Dim>;
using GlyphRun = R2D::GlyphRun<Provider, Dim>;
using GlyphInstance = R2D::GlyphInstance<Provider, Dim>;
using Codepoint = R2D::Codepoint<Provider, Dim>;
using ShapingRun = R2D::ShapingRun<Provider, Dim>;
using ShapedGlyph = R2D::ShapedGlyph<Provider, Dim>;
using GlyphAtlasEntry = R2D::GlyphAtlasEntry<Provider, Dim>;
using FontMetrics = R2D::FontMetrics<Provider, Dim>;

template<class Component>
consteval void requireTextComponent()
{
    static_assert(R2D::StrictPodComponent<Component>);
    static_assert(R2D::SupportedRenderComponent<Provider, Dim, Component>);
    static_assert(std::is_trivial_v<Component>);
    static_assert(std::is_standard_layout_v<Component>);
    static_assert(std::is_trivially_copyable_v<Component>);
    static_assert(std::is_aggregate_v<Component>);
}

int main()
{
    requireTextComponent<Text>();
    requireTextComponent<TextState>();
    requireTextComponent<TextDirtyRange>();
    requireTextComponent<Utf8Slice>();
    requireTextComponent<FontRef>();
    requireTextComponent<FontAtlasRef>();
    requireTextComponent<GlyphRun>();
    requireTextComponent<GlyphInstance>();
    requireTextComponent<Codepoint>();
    requireTextComponent<ShapingRun>();
    requireTextComponent<ShapedGlyph>();
    requireTextComponent<GlyphAtlasEntry>();
    requireTextComponent<FontMetrics>();

    constexpr Utf8Slice kUtf8Slice{
        .buffer_id = 3U,
        .byte_offset = 16U,
        .byte_count = 5U,
    };
    static_assert(kUtf8Slice.byte_count == 5U);

    constexpr Text kText{
        .source_id = 10U,
        .font_id = 2U,
        .utf8_buffer_id = kUtf8Slice.buffer_id,
        .utf8_offset = kUtf8Slice.byte_offset,
        .utf8_size = kUtf8Slice.byte_count,
        .color_rgba8 = 0xFFFFFFFFU,
        .pixel_size = 18.0F,
        .layer = 4U,
        .flags = 0U,
    };
    static_assert(kText.utf8_size == kUtf8Slice.byte_count);

    constexpr TextState kTextState{
        .source_id = kText.source_id,
        .font_id = kText.font_id,
        .utf8_buffer_id = kText.utf8_buffer_id,
        .utf8_offset = kText.utf8_offset,
        .utf8_size = kText.utf8_size,
        .color_rgba8 = kText.color_rgba8,
        .pixel_size = kText.pixel_size,
        .layer = kText.layer,
        .flags = kText.flags,
        .glyph_first = 0U,
        .glyph_count = kText.utf8_size,
    };
    static_assert(kTextState.glyph_count == kText.utf8_size);

    constexpr TextDirtyRange kDirtyRange{
        .source_text_index = 0U,
        .previous_glyph_first = 0U,
        .previous_glyph_count = 0U,
        .new_glyph_first = 0U,
        .new_glyph_count = kText.utf8_size,
        .flags = R2D::kTextDirtyCreatedFlag,
    };
    static_assert(kDirtyRange.new_glyph_count == kText.utf8_size);

    R2DT::FrameComponentStorage<Provider, Dim, GlyphRun> glyph_runs;
    glyph_runs.reserve(1U);
    glyph_runs.push(GlyphRun{
        .source_text_index = 0U,
        .glyph_first = 0U,
        .glyph_count = 2U,
        .atlas_id = 7U,
        .atlas_generation = 1U,
        .flags = 0U,
    });

    R2DT::FrameComponentStorage<Provider, Dim, GlyphInstance> glyph_instances;
    glyph_instances.reserve(2U);
    glyph_instances.push(GlyphInstance{
        .glyph_run_index = 0U,
        .glyph_id = 41U,
        .atlas_rect = R2D::makeAabb2(0.0F, 0.0F, 0.25F, 0.5F),
        .position_x = 10.0F,
        .position_y = 20.0F,
        .width = 8.0F,
        .height = 16.0F,
        .color_rgba8 = 0xFFFFFFFFU,
        .sort_key = 4U,
        .layer = 1U,
        .flags = 0U,
    });
    glyph_instances.push(GlyphInstance{
        .glyph_run_index = 0U,
        .glyph_id = 42U,
        .atlas_rect = R2D::makeAabb2(0.25F, 0.0F, 0.5F, 0.5F),
        .position_x = 18.0F,
        .position_y = 20.0F,
        .width = 8.0F,
        .height = 16.0F,
        .color_rgba8 = 0xFFFFFFFFU,
        .sort_key = 5U,
        .layer = 1U,
        .flags = 0U,
    });

    const auto run_view = glyph_runs.view();
    const auto instance_view = glyph_instances.view();
    assert(run_view.count == 1U);
    assert(instance_view.count == 2U);
    assert(run_view.data[0].glyph_count == instance_view.count);
    assert(instance_view.data[1U].glyph_id == 42U);
    assert(instance_view.data[1U].position_x > instance_view.data[0U].position_x);
    assert(R2D::aabb2Min(instance_view.data[1U].atlas_rect).x == 0.25F);
    assert(R2D::aabb2Max(instance_view.data[1U].atlas_rect).x == 0.5F);

    const FontAtlasRef atlas{
        .font_id = kText.font_id,
        .atlas_id = run_view.data[0].atlas_id,
        .generation = run_view.data[0].atlas_generation,
        .texture_id = 99U,
        .texture_generation = 0U,
        .flags = 0U,
    };
    assert(atlas.generation == 1U);
    assert(atlas.texture_id == 99U);

    // Stage 19B contract checks: extended GlyphInstance/FontRef and the new
    // shaping-pipeline components.
    const GlyphInstance quad_glyph{
        .glyph_run_index = 0U,
        .glyph_id = 65U,
        .atlas_rect = R2D::makeAabb2(0.0F, 0.0F, 0.5F, 1.0F),
        .position_x = 4.0F,
        .position_y = 7.0F,
        .width = 6.0F,
        .height = 12.0F,
        .color_rgba8 = 0xFFFFFFFFU,
        .sort_key = 0U,
        .layer = 0U,
        .flags = 0U,
    };
    assert(quad_glyph.width == 6.0F);
    assert(quad_glyph.height == 12.0F);

    constexpr FontRef kFontRef{.id = 2U, .generation = 5U, .flags = 0U};
    static_assert(kFontRef.generation == 5U);

    constexpr Codepoint kCodepoint{.text_index = 0U, .codepoint = 0x41U, .byte_offset = 0U, .flags = 0U};
    static_assert(kCodepoint.codepoint == 0x41U);

    constexpr ShapingRun kShapingRun{
        .text_index = 0U,
        .codepoint_first = 0U,
        .codepoint_count = 3U,
        .font_id = 2U,
        .script = 0U,
        .direction = R2D::kTextDirectionRtl,
        .bidi_level = 1U,
        .flags = 0U,
    };
    static_assert(kShapingRun.direction == R2D::kTextDirectionRtl);
    static_assert(kShapingRun.bidi_level == 1U);

    constexpr ShapedGlyph kShapedGlyph{
        .run_index = 0U,
        .glyph_id = 41U,
        .cluster = 0U,
        .x_advance = 9.5F,
        .y_advance = 0.0F,
        .x_offset = 0.0F,
        .y_offset = 0.0F,
        .flags = 0U,
    };
    static_assert(kShapedGlyph.x_advance == 9.5F);

    const GlyphAtlasEntry entry{
        .font_id = 2U,
        .generation = 5U,
        .glyph_id = 41U,
        .pixel_size = 18.0F,
        .atlas_id = 7U,
        .atlas_generation = 1U,
        .atlas_rect = R2D::makeAabb2(0.0F, 0.0F, 0.25F, 0.5F),
        .bitmap_width = 6.0F,
        .bitmap_height = 12.0F,
        .bearing_x = 1.0F,
        .bearing_y = 11.0F,
        .flags = 0U,
    };
    assert(entry.bitmap_height == 12.0F);
    assert(R2D::aabb2Max(entry.atlas_rect).x == 0.25F);

    constexpr FontMetrics kFontMetrics{
        .font_id = 2U,
        .generation = 5U,
        .pixel_size = 18.0F,
        .ascent = 14.0F,
        .descent = -4.0F,
        .line_height = 21.0F,
        .flags = 0U,
    };
    static_assert(kFontMetrics.line_height == 21.0F);

    return 0;
}
