#include <Render2D/Render2D.hpp>

#include <array>
#include <cassert>
#include <span>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using LocalBounds = R2D::LocalBounds<Provider, Dim>;
using WorldBounds = R2D::WorldBounds<Provider, Dim>;
using WorldTransform = R2D::WorldTransform<Provider, Dim>;

[[nodiscard]] bool nearlyEqual(float left_, float right_) noexcept
{
    constexpr float kEpsilon = 0.0001F;
    return MMath::abs(left_ - right_) <= kEpsilon;
}

[[nodiscard]] R2D::Aabb2 referenceTransformBounds(R2D::Aabb2 bounds_, R2D::Mat3 matrix_) noexcept
{
    const R2D::Vec2 min = R2D::aabb2Min(bounds_);
    const R2D::Vec2 max = R2D::aabb2Max(bounds_);
    const std::array<R2D::Vec2, 4U> corners{{
        MMath::mat3TransformPoint(matrix_, {.x = min.x, .y = min.y}),
        MMath::mat3TransformPoint(matrix_, {.x = max.x, .y = min.y}),
        MMath::mat3TransformPoint(matrix_, {.x = min.x, .y = max.y}),
        MMath::mat3TransformPoint(matrix_, {.x = max.x, .y = max.y}),
    }};

    float min_x = corners[0U].x;
    float min_y = corners[0U].y;
    float max_x = corners[0U].x;
    float max_y = corners[0U].y;
    for (R2D::Usize index = 1U; index < corners.size(); ++index) {
        min_x = MMath::min(min_x, corners[index].x);
        min_y = MMath::min(min_y, corners[index].y);
        max_x = MMath::max(max_x, corners[index].x);
        max_y = MMath::max(max_y, corners[index].y);
    }

    return R2D::makeAabb2(min_x, min_y, max_x, max_y);
}

void assertAabbNear(R2D::Aabb2 actual_, R2D::Aabb2 expected_) noexcept
{
    const R2D::Vec2 actual_min = R2D::aabb2Min(actual_);
    const R2D::Vec2 actual_max = R2D::aabb2Max(actual_);
    const R2D::Vec2 expected_min = R2D::aabb2Min(expected_);
    const R2D::Vec2 expected_max = R2D::aabb2Max(expected_);

    assert(nearlyEqual(actual_min.x, expected_min.x));
    assert(nearlyEqual(actual_min.y, expected_min.y));
    assert(nearlyEqual(actual_max.x, expected_max.x));
    assert(nearlyEqual(actual_max.y, expected_max.y));
}

int main()
{
    const R2D::Aabb2 local_box = R2D::makeAabb2(-1.0F, -0.5F, 2.0F, 1.5F);
    const std::array<LocalBounds, 4U> local_bounds{{
        {.source_id = 0U, .bounds = local_box},
        {.source_id = 1U, .bounds = local_box},
        {.source_id = 2U, .bounds = local_box},
        {.source_id = 3U, .bounds = local_box},
    }};

    const std::array<WorldTransform, 4U> world_transforms{{
        {
            .source_id = 0U,
            .affine = MMath::mat3FromTranslation({.x = 3.0F, .y = -2.0F}),
        },
        {
            .source_id = 1U,
            .affine = MMath::mat3FromScale({.x = -2.0F, .y = 0.5F}),
        },
        {
            .source_id = 2U,
            .affine = {
                .m00 = 0.0F,
                .m01 = -1.0F,
                .m02 = 1.0F,
                .m10 = 1.0F,
                .m11 = 0.0F,
                .m12 = 2.0F,
                .m20 = 0.0F,
                .m21 = 0.0F,
                .m22 = 1.0F,
            },
        },
        {
            .source_id = 3U,
            .affine = {
                .m00 = 1.25F,
                .m01 = 0.75F,
                .m02 = -4.0F,
                .m10 = -0.5F,
                .m11 = 2.0F,
                .m12 = 3.0F,
                .m20 = 0.0F,
                .m21 = 0.0F,
                .m22 = 1.0F,
            },
        },
    }};

    std::array<WorldBounds, local_bounds.size()> world_bounds{};
    const auto result = R2D::BoundsSystem<Provider, Dim>::run(
        world_transforms,
        local_bounds,
        world_bounds);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.write_count == local_bounds.size());

    for (R2D::Usize index = 0U; index < world_bounds.size(); ++index) {
        assert(world_bounds[index].source_id == local_bounds[index].source_id);
        assertAabbNear(
            world_bounds[index].bounds,
            referenceTransformBounds(local_box, world_transforms[index].affine));
    }

    std::array<WorldBounds, 1U> short_output{};
    const auto short_result = R2D::BoundsSystem<Provider, Dim>::run(
        world_transforms,
        local_bounds,
        short_output);
    assert(short_result.code == R2D::SystemStatusCode::InsufficientCapacity);

    const std::span<const WorldTransform> short_transforms{
        world_transforms.data(),
        world_transforms.size() - 1U,
    };
    const auto invalid_result = R2D::BoundsSystem<Provider, Dim>::run(
        short_transforms,
        local_bounds,
        world_bounds);
    assert(invalid_result.code == R2D::SystemStatusCode::InvalidInput);

    return 0;
}
