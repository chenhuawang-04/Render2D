#include <Render2D/Render2D.hpp>

#include <array>
#include <cassert>
#include <span>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;

using Transform = R2D::Transform<Provider, Dim>;
using WorldTransform = R2D::WorldTransform<Provider, Dim>;
using LocalBounds = R2D::LocalBounds<Provider, Dim>;
using WorldBounds = R2D::WorldBounds<Provider, Dim>;
using Camera = R2D::Camera<Provider, Dim>;
using VisibilityMask = R2D::VisibilityMask<Provider, Dim>;
using VisibleItem = R2D::VisibleItem<Provider, Dim>;
using Sprite = R2D::Sprite<Provider, Dim>;
using DrawCommand = R2D::DrawCommand<Provider, Dim>;
using BatchCommand = R2D::BatchCommand<Provider, Dim>;

static_assert(R2D::SupportedRenderComponent<Provider, Dim, VisibleItem>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, DrawCommand>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, BatchCommand>);

int main()
{
    constexpr auto kVisibleMask = 0xFFFFFFFFU;
    constexpr auto kHiddenPosition = 100.0F;

    const std::array<Transform, 3U> transforms{{
        {
            .source_id = 0U,
            .position_x = 0.0F,
            .position_y = 0.0F,
            .rotation_radians = 0.0F,
            .scale_x = 1.0F,
            .scale_y = 1.0F,
        },
        {
            .source_id = 1U,
            .position_x = 1.0F,
            .position_y = 0.0F,
            .rotation_radians = 0.0F,
            .scale_x = 1.0F,
            .scale_y = 1.0F,
        },
        {
            .source_id = 2U,
            .position_x = kHiddenPosition,
            .position_y = kHiddenPosition,
            .rotation_radians = 0.0F,
            .scale_x = 1.0F,
            .scale_y = 1.0F,
        },
    }};

    const std::array<LocalBounds, 3U> local_bounds{{
        {.source_id = 0U, .bounds = R2D::makeAabb2(-0.5F, -0.5F, 0.5F, 0.5F)},
        {.source_id = 1U, .bounds = R2D::makeAabb2(-0.5F, -0.5F, 0.5F, 0.5F)},
        {.source_id = 2U, .bounds = R2D::makeAabb2(-0.5F, -0.5F, 0.5F, 0.5F)},
    }};

    const std::array<VisibilityMask, 3U> visibility_masks{{
        {.mask = kVisibleMask},
        {.mask = kVisibleMask},
        {.mask = kVisibleMask},
    }};

    const std::array<Sprite, 3U> sprites{{
        {
            .source_id = 0U,
            .texture_id = 10U,
            .texture_generation = 0U,
            .texture_region_id = 0U,
            .texture_region_generation = 0U,
            .material_id = 20U,
            .material_generation = 0U,
            .color_rgba8 = kVisibleMask,
            .layer = 1U,
            .flags = 0U,
        },
        {
            .source_id = 1U,
            .texture_id = 10U,
            .texture_generation = 0U,
            .texture_region_id = 0U,
            .texture_region_generation = 0U,
            .material_id = 20U,
            .material_generation = 0U,
            .color_rgba8 = kVisibleMask,
            .layer = 1U,
            .flags = 0U,
        },
        {
            .source_id = 2U,
            .texture_id = 30U,
            .texture_generation = 0U,
            .texture_region_id = 0U,
            .texture_region_generation = 0U,
            .material_id = 40U,
            .material_generation = 0U,
            .color_rgba8 = kVisibleMask,
            .layer = 1U,
            .flags = 0U,
        },
    }};

    constexpr Camera kCamera{
        .source_id = 0U,
        .position_x = 0.0F,
        .position_y = 0.0F,
        .rotation_radians = 0.0F,
        .viewport_width = 10.0F,
        .viewport_height = 10.0F,
        .near_z = 0.0F,
        .far_z = 1.0F,
        .layer_mask = kVisibleMask,
        .flags = 0U,
    };

    std::array<WorldTransform, transforms.size()> world_transforms{};
    auto result = R2D::TransformSystem<Provider, Dim>::run(transforms, world_transforms);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.write_count == transforms.size());
    assert(world_transforms[1U].affine.m02 == 1.0F);

    std::array<WorldBounds, local_bounds.size()> world_bounds{};
    result = R2D::BoundsSystem<Provider, Dim>::run(world_transforms, local_bounds, world_bounds);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(R2D::aabb2Min(world_bounds[0U].bounds).x == -0.5F);
    assert(R2D::aabb2Max(world_bounds[1U].bounds).x == 1.5F);

    std::array<VisibleItem, local_bounds.size()> visible_items{};
    result = R2D::CullingSystem<Provider, Dim>::run(kCamera, world_bounds, visibility_masks, visible_items);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.write_count == 2U);
    assert(visible_items[0U].source_index == 0U);
    assert(visible_items[1U].source_index == 1U);

    std::array<DrawCommand, local_bounds.size()> draw_commands{};
    result = R2D::CommandBuildSystem<Provider, Dim>::run(
        std::span<const VisibleItem>{visible_items.data(), result.write_count},
        sprites,
        draw_commands);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.write_count == 2U);
    assert(draw_commands[0U].texture_id == 10U);
    assert(draw_commands[1U].material_id == 20U);

    std::array<BatchCommand, local_bounds.size()> batch_commands{};
    result = R2D::BatchSystem<Provider, Dim>::run(
        std::span<const DrawCommand>{draw_commands.data(), result.write_count},
        batch_commands);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.read_count == 2U);
    assert(result.write_count == 1U);
    assert(batch_commands[0U].draw_first == 0U);
    assert(batch_commands[0U].draw_count == 2U);

    return 0;
}
