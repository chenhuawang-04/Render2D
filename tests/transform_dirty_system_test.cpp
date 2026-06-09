#include "support/TestHarness.hpp"

#include <Render2D/Render2D.hpp>

#include <array>
#include <span>

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Transform = R2D::Transform<Provider, Dim>;
using TransformDirtyItem = R2D::TransformDirtyItem<Provider, Dim>;
using WorldTransform = R2D::WorldTransform<Provider, Dim>;
using LocalBounds = R2D::LocalBounds<Provider, Dim>;
using WorldBounds = R2D::WorldBounds<Provider, Dim>;

[[nodiscard]] bool aabbNear(R2D::Aabb2 left_, R2D::Aabb2 right_) noexcept
{
    return R2D::aabb2NearEqual(left_, right_, 0.0001F);
}

int main()
{
    R2DT::TestContext context{};

    std::array<Transform, 3U> transforms{{
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
            .position_x = 2.0F,
            .position_y = 0.0F,
            .rotation_radians = 0.0F,
            .scale_x = 1.0F,
            .scale_y = 1.0F,
        },
        {
            .source_id = 2U,
            .position_x = 4.0F,
            .position_y = 0.0F,
            .rotation_radians = 0.0F,
            .scale_x = 1.0F,
            .scale_y = 1.0F,
        },
    }};

    std::array<WorldTransform, transforms.size()> world_transforms{};
    auto result = R2D::TransformSystem<Provider, Dim>::run(transforms, world_transforms);
    R2D_TEST_REQUIRE(context, result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, result.write_count, transforms.size());
    R2D_TEST_CHECK(context, world_transforms[1U].affine.m02 == 2.0F);

    const WorldTransform original_first_transform = world_transforms[0U];
    transforms[1U].position_x = 8.0F;
    transforms[1U].position_y = -3.0F;
    transforms[1U].scale_x = 2.0F;
    transforms[1U].scale_y = 0.5F;

    const std::array<TransformDirtyItem, 1U> dirty_items{{
        {
            .source_index = 1U,
            .flags = 0U,
        },
    }};
    result = R2D::TransformSystem<Provider, Dim>::runDirty(transforms, dirty_items, world_transforms);
    R2D_TEST_CHECK(context, result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, result.read_count, 1U);
    R2D_TEST_CHECK_EQ(context, result.write_count, 1U);
    R2D_TEST_CHECK(context, world_transforms[0U].affine.m02 == original_first_transform.affine.m02);
    R2D_TEST_CHECK(context, world_transforms[1U].affine.m00 == 2.0F);
    R2D_TEST_CHECK(context, world_transforms[1U].affine.m02 == 8.0F);
    R2D_TEST_CHECK(context, world_transforms[1U].affine.m12 == -3.0F);

    const std::array<LocalBounds, transforms.size()> local_bounds{{
        {.source_id = 0U, .bounds = R2D::makeAabb2(-1.0F, -1.0F, 1.0F, 1.0F)},
        {.source_id = 1U, .bounds = R2D::makeAabb2(-1.0F, -1.0F, 1.0F, 1.0F)},
        {.source_id = 2U, .bounds = R2D::makeAabb2(-1.0F, -1.0F, 1.0F, 1.0F)},
    }};
    std::array<WorldBounds, transforms.size()> world_bounds{};
    result = R2D::BoundsSystem<Provider, Dim>::run(world_transforms, local_bounds, world_bounds);
    R2D_TEST_REQUIRE(context, result.code == R2D::SystemStatusCode::Ok);
    const R2D::Aabb2 original_first_bounds = world_bounds[0U].bounds;

    transforms[1U].position_x = 10.0F;
    result = R2D::TransformSystem<Provider, Dim>::runDirty(transforms, dirty_items, world_transforms);
    R2D_TEST_REQUIRE(context, result.code == R2D::SystemStatusCode::Ok);
    result = R2D::BoundsSystem<Provider, Dim>::runDirty(
        world_transforms,
        local_bounds,
        dirty_items,
        world_bounds);
    R2D_TEST_CHECK(context, result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, result.write_count, 1U);
    R2D_TEST_CHECK(context, aabbNear(world_bounds[0U].bounds, original_first_bounds));
    R2D_TEST_CHECK(context, R2D::aabb2Min(world_bounds[1U].bounds).x == 8.0F);
    R2D_TEST_CHECK(context, R2D::aabb2Max(world_bounds[1U].bounds).x == 12.0F);

    const std::array<TransformDirtyItem, 1U> invalid_dirty_items{{
        {
            .source_index = 3U,
            .flags = 0U,
        },
    }};
    result = R2D::TransformSystem<Provider, Dim>::runDirty(
        transforms,
        invalid_dirty_items,
        world_transforms);
    R2D_TEST_CHECK(context, result.code == R2D::SystemStatusCode::InvalidInput);
    result = R2D::BoundsSystem<Provider, Dim>::runDirty(
        world_transforms,
        local_bounds,
        invalid_dirty_items,
        world_bounds);
    R2D_TEST_CHECK(context, result.code == R2D::SystemStatusCode::InvalidInput);

    return context.result();
}
