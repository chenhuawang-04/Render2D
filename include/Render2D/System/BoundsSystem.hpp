#pragma once

#include "Render2D/Component/Bounds.hpp"
#include "Render2D/Component/Transform.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Meta/Domain.hpp"

#include <algorithm>
#include <span>

namespace Render2D {

template<class Provider, class Dim>
struct BoundsSystem {
    static SystemResult run(
        std::span<const WorldTransform<Provider, Dim>> world_transforms_,
        std::span<const LocalBounds<Provider, Dim>> local_bounds_,
        std::span<WorldBounds<Provider, Dim>> world_bounds_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (world_transforms_.size() != local_bounds_.size()) {
                return {
                    .code = SystemStatusCode::InvalidInput,
                    .read_count = static_cast<U32>(std::min(world_transforms_.size(), local_bounds_.size())),
                    .write_count = 0U,
                };
            }
            if (world_bounds_.size() < local_bounds_.size()) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = static_cast<U32>(local_bounds_.size()),
                    .write_count = static_cast<U32>(world_bounds_.size()),
                };
            }

            for (Usize index = 0U; index < local_bounds_.size(); ++index) {
                world_bounds_[index] = WorldBounds<Provider, Dim>{
                    .source_id = local_bounds_[index].source_id,
                    .bounds = transformBounds(local_bounds_[index].bounds, world_transforms_[index].affine),
                };
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(local_bounds_.size()),
                .write_count = static_cast<U32>(local_bounds_.size()),
            };
        }
    }

private:
    static Aabb2 transformBounds(const Aabb2& bounds_, const Affine2X3& affine_) noexcept
    {
        const auto x0 = affine_.m00 * bounds_.min_x + affine_.m01 * bounds_.min_y + affine_.m02;
        const auto y0 = affine_.m10 * bounds_.min_x + affine_.m11 * bounds_.min_y + affine_.m12;
        const auto x1 = affine_.m00 * bounds_.max_x + affine_.m01 * bounds_.min_y + affine_.m02;
        const auto y1 = affine_.m10 * bounds_.max_x + affine_.m11 * bounds_.min_y + affine_.m12;
        const auto x2 = affine_.m00 * bounds_.min_x + affine_.m01 * bounds_.max_y + affine_.m02;
        const auto y2 = affine_.m10 * bounds_.min_x + affine_.m11 * bounds_.max_y + affine_.m12;
        const auto x3 = affine_.m00 * bounds_.max_x + affine_.m01 * bounds_.max_y + affine_.m02;
        const auto y3 = affine_.m10 * bounds_.max_x + affine_.m11 * bounds_.max_y + affine_.m12;

        return {
            .min_x = std::min({x0, x1, x2, x3}),
            .min_y = std::min({y0, y1, y2, y3}),
            .max_x = std::max({x0, x1, x2, x3}),
            .max_y = std::max({y0, y1, y2, y3}),
        };
    }
};

} // namespace Render2D
