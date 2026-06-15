#pragma once

#include "Render2D/Component/Bounds.hpp"
#include "Render2D/Component/Camera.hpp"
#include "Render2D/Component/Command.hpp"
#include "Render2D/Component/Sprite.hpp"
#include "Render2D/Component/Transform.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Meta/Domain.hpp"

#include <span>

namespace Render2D {

// Stage 24 Track 1: fused transform -> bounds -> culling front-end. Produces the
// SAME outputs as running TransformSystem::run, BoundsSystem::run, then
// CullingSystem::run in sequence -- the dense WorldTransform array and the
// compacted VisibleItem stream -- but in a single pass that never materializes
// the intermediate WorldBounds array (the world AABB lives only in registers).
//
// WorldTransform is NOT dropped: SpriteInstanceBuildSystem reads
// world_transforms_[source_index].affine downstream (see SpriteInstanceSystem),
// so the affine must persist. WorldBounds, by contrast, is consumed solely by
// the cull test, so the fused pass keeps it in a register and writes nothing.
// This collapses the front-end's per-element memory traffic (no WorldBounds
// write + no WorldTransform/WorldBounds re-read between stages) and improves
// locality (each element is transformed, bounded, and culled while hot in
// cache) -- targeting the bandwidth-bound front-end identified in the Stage 21
// at-scale profile (docs/architecture/BENCHMARK_BASELINE.md).
//
// Byte-identity is a hard contract: the per-element affine math is replicated
// verbatim from TransformSystem::writeWorldTransform, the bounds math from
// BoundsSystem::transformBounds, and the cull decision mirrors
// CullingSystem::run (same camera bounds, same mask/intersect test, same
// VisibleItem fields and emission order). tests/spatial_cull_system_test.cpp
// memcmp-verifies the fused output against the three-system chain. The granular
// systems remain the deterministic reference and are intentionally untouched.
template<class Provider, class Dim>
struct SpatialCullSystem {
    static SystemResult run(
        const Camera<Provider, Dim>& camera_,
        std::span<const Transform<Provider, Dim>> transforms_,
        std::span<const LocalBounds<Provider, Dim>> local_bounds_,
        std::span<const VisibilityMask<Provider, Dim>> visibility_masks_,
        std::span<WorldTransform<Provider, Dim>> world_transforms_,
        std::span<VisibleItem<Provider, Dim>> visible_items_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isSystemResultCountRepresentable(transforms_.size()) ||
                !isSystemResultCountRepresentable(local_bounds_.size()) ||
                !isSystemResultCountRepresentable(visibility_masks_.size()) ||
                !isSystemResultCountRepresentable(world_transforms_.size()) ||
                !isSystemResultCountRepresentable(visible_items_.size())) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            // Mirrors BoundsSystem's transforms==local_bounds count contract.
            if (transforms_.size() != local_bounds_.size()) {
                const Usize read_count = transforms_.size() < local_bounds_.size() ?
                    transforms_.size() :
                    local_bounds_.size();
                return {
                    .code = SystemStatusCode::InvalidInput,
                    .read_count = static_cast<U32>(read_count),
                    .write_count = 0U,
                };
            }
            // Mirrors TransformSystem's world-transform capacity contract.
            if (world_transforms_.size() < transforms_.size()) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = static_cast<U32>(transforms_.size()),
                    .write_count = static_cast<U32>(world_transforms_.size()),
                };
            }
            // Mirrors CullingSystem's mask count contract.
            if (!visibility_masks_.empty() && visibility_masks_.size() != transforms_.size()) {
                return {
                    .code = SystemStatusCode::InvalidInput,
                    .read_count = static_cast<U32>(transforms_.size()),
                    .write_count = 0U,
                };
            }

            const auto* const transforms = transforms_.data();
            const auto* const local_bounds = local_bounds_.data();
            const bool has_masks = !visibility_masks_.empty();
            const auto* const visibility_masks = visibility_masks_.data();
            auto* const world_transforms = world_transforms_.data();
            const Usize item_count = transforms_.size();
            const auto camera_bounds = cameraBounds(camera_);

            Usize write_index = 0U;
            for (Usize index = 0U; index < item_count; ++index) {
                // WorldTransform is a real downstream output; write it for every
                // element exactly as TransformSystem::run would, regardless of
                // visibility. The affine is kept in a local and reused for the
                // bounds test (instead of re-read from world_transforms[index]),
                // which avoids a store-to-load dependency on the compute-bound
                // rotating path; the stored and reused bits are identical.
                const auto& transform = transforms[index];
                const Mat3 affine = computeAffine(transform);
                world_transforms[index] = {.source_id = transform.source_id, .affine = affine};

                const auto mask = has_masks ? visibility_masks[index].mask : camera_.layer_mask;
                if ((mask & camera_.layer_mask) == 0U) {
                    continue;
                }
                const Aabb2 world_bounds = transformBounds(local_bounds[index].bounds, affine);
                if (!Render2D::aabb2Intersects(world_bounds, camera_bounds)) {
                    continue;
                }
                if (write_index >= visible_items_.size()) {
                    return {
                        .code = SystemStatusCode::InsufficientCapacity,
                        .read_count = static_cast<U32>(index),
                        .write_count = static_cast<U32>(write_index),
                    };
                }

                visible_items_[write_index] = {
                    .source_index = static_cast<U32>(index),
                    .layer = 0U,
                    .sort_key = static_cast<U32>(index),
                    .flags = 0U,
                };
                ++write_index;
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(item_count),
                .write_count = static_cast<U32>(write_index),
            };
        }
    }

private:
    // Replicated verbatim from TransformSystem::writeWorldTransform (factored to
    // return the affine) so the dense WorldTransform output is byte-identical to
    // TransformSystem::run.
    static Mat3 computeAffine(const Transform<Provider, Dim>& transform_) noexcept
    {
        if (transform_.rotation_radians == 0.0F) {
            return {
                .m00 = transform_.scale_x,
                .m01 = 0.0F,
                .m02 = transform_.position_x,
                .m10 = 0.0F,
                .m11 = transform_.scale_y,
                .m12 = transform_.position_y,
                .m20 = 0.0F,
                .m21 = 0.0F,
                .m22 = 1.0F,
            };
        }

        MMath::SinCos sin_cos{};
        MMath::sincos(MMath::Angle{.value = transform_.rotation_radians}, &sin_cos);
        return {
            .m00 = sin_cos.cos * transform_.scale_x,
            .m01 = -sin_cos.sin * transform_.scale_y,
            .m02 = transform_.position_x,
            .m10 = sin_cos.sin * transform_.scale_x,
            .m11 = sin_cos.cos * transform_.scale_y,
            .m12 = transform_.position_y,
            .m20 = 0.0F,
            .m21 = 0.0F,
            .m22 = 1.0F,
        };
    }

    // Replicated verbatim from BoundsSystem::transformBounds so the in-register
    // world AABB matches the value CullingSystem would read from WorldBounds.
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

    // Replicated verbatim from CullingSystem::cameraBounds.
    static Aabb2 cameraBounds(const Camera<Provider, Dim>& camera_) noexcept
    {
        const auto half_width = camera_.viewport_width * 0.5F;
        const auto half_height = camera_.viewport_height * 0.5F;
        return makeAabb2(
            camera_.position_x - half_width,
            camera_.position_y - half_height,
            camera_.position_x + half_width,
            camera_.position_y + half_height);
    }
};

} // namespace Render2D
