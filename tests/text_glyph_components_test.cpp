#include "support/TemporaryEcsStorage.hpp"

#include <Render2D/Render2D.hpp>

#include <cassert>
#include <type_traits>

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Text = R2D::Text<Provider, Dim>;
using Utf8Slice = R2D::Utf8Slice<Provider, Dim>;
using FontRef = R2D::FontRef<Provider, Dim>;
using FontAtlasRef = R2D::FontAtlasRef<Provider, Dim>;
using GlyphRun = R2D::GlyphRun<Provider, Dim>;
using GlyphInstance = R2D::GlyphInstance<Provider, Dim>;

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
    requireTextComponent<Utf8Slice>();
    requireTextComponent<FontRef>();
    requireTextComponent<FontAtlasRef>();
    requireTextComponent<GlyphRun>();
    requireTextComponent<GlyphInstance>();

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
        .atlas_rect = {.min_x = 0.0F, .min_y = 0.0F, .max_x = 0.25F, .max_y = 0.5F},
        .position_x = 10.0F,
        .position_y = 20.0F,
        .color_rgba8 = 0xFFFFFFFFU,
        .sort_key = 4U,
        .layer = 1U,
        .flags = 0U,
    });
    glyph_instances.push(GlyphInstance{
        .glyph_run_index = 0U,
        .glyph_id = 42U,
        .atlas_rect = {.min_x = 0.25F, .min_y = 0.0F, .max_x = 0.5F, .max_y = 0.5F},
        .position_x = 18.0F,
        .position_y = 20.0F,
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

    const FontAtlasRef atlas{
        .font_id = kText.font_id,
        .atlas_id = run_view.data[0].atlas_id,
        .generation = run_view.data[0].atlas_generation,
        .texture_id = 99U,
        .flags = 0U,
    };
    assert(atlas.generation == 1U);
    assert(atlas.texture_id == 99U);

    return 0;
}
