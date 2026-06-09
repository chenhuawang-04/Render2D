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
            if (!isSystemResultCountRepresentable(world_transforms_.size()) ||
                !isSystemResultCountRepresentable(local_bounds_.size()) ||
                !isSystemResultCountRepresentable(world_bounds_.size())) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
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

            const auto* const world_transforms = world_transforms_.data();
            const auto* const local_bounds = local_bounds_.data();
            auto* const world_bounds = world_bounds_.data();
            const Usize bounds_count = local_bounds_.size();
            for (Usize index = 0U; index < bounds_count; ++index) {
                writeWorldBounds(world_transforms[index], local_bounds[index], world_bounds[index]);
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(bounds_count),
                .write_count = static_cast<U32>(bounds_count),
            };
        }
    }

    static SystemResult runDirty(
        std::span<const WorldTransform<Provider, Dim>> world_transforms_,
        std::span<const LocalBounds<Provider, Dim>> local_bounds_,
        std::span<const TransformDirtyItem<Provider, Dim>> dirty_items_,
        std::span<WorldBounds<Provider, Dim>> world_bounds_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isSystemResultCountRepresentable(world_transforms_.size()) ||
                !isSystemResultCountRepresentable(local_bounds_.size()) ||
                !isSystemResultCountRepresentable(dirty_items_.size()) ||
                !isSystemResultCountRepresentable(world_bounds_.size())) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }

            const auto* const world_transforms = world_transforms_.data();
            const auto* const local_bounds = local_bounds_.data();
            const auto* const dirty_items = dirty_items_.data();
            auto* const world_bounds = world_bounds_.data();
            const Usize world_transform_count = world_transforms_.size();
            const Usize local_bounds_count = local_bounds_.size();
            const Usize world_bounds_count = world_bounds_.size();
            const Usize dirty_count = dirty_items_.size();
            for (Usize dirty_index = 0U; dirty_index < dirty_count; ++dirty_index) {
                const Usize source_index = dirty_items[dirty_index].source_index;
                if (source_index >= world_transform_count ||
                    source_index >= local_bounds_count ||
                    source_index >= world_bounds_count) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(dirty_index),
                        .write_count = static_cast<U32>(dirty_index),
                    };
                }

                writeWorldBounds(
                    world_transforms[source_index],
                    local_bounds[source_index],
                    world_bounds[source_index]);
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(dirty_count),
                .write_count = static_cast<U32>(dirty_count),
            };
        }
    }

private:
    static void writeWorldBounds(
        const WorldTransform<Provider, Dim>& world_transform_,
        const LocalBounds<Provider, Dim>& local_bounds_,
        WorldBounds<Provider, Dim>& out_world_bounds_) noexcept
    {
        out_world_bounds_ = {
            .source_id = local_bounds_.source_id,
            .bounds = transformBounds(local_bounds_.bounds, world_transform_.affine),
        };
    }

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
