#include "support/TemporaryEcsStorage.hpp"

#include <Render2D/Render2D.hpp>

#include <cassert>

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Sprite = R2D::Sprite<Provider, Dim>;
using DrawCommand = R2D::DrawCommand<Provider, Dim>;

static_assert(R2D::SupportedRenderComponent<Provider, Dim, Sprite>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, DrawCommand>);

int main()
{
    try {
        R2DT::PersistentComponentStorage<Provider, Dim, Sprite> sprites;
        sprites.reserve(2U);

        const Sprite first_sprite{
            .source_id = 7U,
            .texture_id = 11U,
            .material_id = 13U,
            .color_rgba8 = 0xFFFFFFFFU,
            .layer = 2U,
            .flags = 0U,
        };
        sprites.push(first_sprite);
        sprites.push(Sprite{
            .source_id = 8U,
            .texture_id = 12U,
            .material_id = 14U,
            .color_rgba8 = 0xFFFFFFFFU,
            .layer = 3U,
            .flags = 1U,
        });

        assert(sprites.size() == 2U);
        assert(sprites.capacity() >= 2U);
        assert(sprites.at(0U).source_id == 7U);

        const auto sprite_view = sprites.view();
        assert(sprite_view.data == sprites.data());
        assert(sprite_view.count == 2U);

        const auto old_capacity = sprites.capacity();
        sprites.clear();
        assert(sprites.size() == 0U);
        assert(sprites.capacity() == old_capacity);

        R2DT::FrameComponentStorage<Provider, Dim, DrawCommand> draw_commands;
        draw_commands.reserve(1U);
        draw_commands.push(DrawCommand{
            .source_index = 0U,
            .material_id = 13U,
            .texture_id = 11U,
            .vertex_first = 0U,
            .vertex_count = 4U,
            .index_first = 0U,
            .index_count = 6U,
            .instance_first = 0U,
            .instance_count = 1U,
            .sort_key = 42U,
            .layer = 2U,
            .flags = 0U,
        });

        const auto draw_view = draw_commands.view();
        assert(draw_view.count == 1U);
        assert(draw_view.data[0].index_count == 6U);

        draw_commands.reset();
        assert(draw_commands.size() == 0U);
        assert(draw_commands.capacity() >= 1U);

        return 0;
    } catch (...) {
        return 1;
    }
}
