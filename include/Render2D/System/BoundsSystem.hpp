#pragma once

#include "Render2D/Component/Bounds.hpp"
#include "Render2D/Component/Transform.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Meta/Domain.hpp"

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
                const Usize read_count = world_transforms_.size() < local_bounds_.size() ?
                    world_transforms_.size() :
                    local_bounds_.size();
                return {
                    .code = SystemStatusCode::InvalidInput,
                    .read_count = static_cast<U32>(read_count),
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
    static Aabb2 transformBounds(Aabb2 bounds_, Mat3 affine_) noexcept
    {
        const Vec2 center = MMath::aabb2Center(bounds_);
        const Vec2 extents = MMath::aabb2Extents(bounds_);
        const Vec2 world_center = MMath::mat3TransformPoint(affine_, center);
        const Vec2 world_extents{
            .x = MMath::abs(affine_.m00) * extents.x + MMath::abs(affine_.m01) * extents.y,
            .y = MMath::abs(affine_.m10) * extents.x + MMath::abs(affine_.m11) * extents.y,
        };
        return makeAabb2FromCenterExtents(world_center, world_extents);
    }
};

} // namespace Render2D
