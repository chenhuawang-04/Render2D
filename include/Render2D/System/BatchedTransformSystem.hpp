#pragma once

#include "Render2D/Component/Transform.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Meta/Domain.hpp"

#include <fast_math/mat3.h>

#include <cstdint>
#include <span>

namespace Render2D {

// Stage 24 Track 2: SIMD-batched world-transform build. Produces the dense
// WorldTransform array via MMath::mat3FromTrsArray, whose AVX2 path computes 8
// sincos at once, targeting the compute-bound rotating workload (sincos-dominated)
// identified in the Stage 21 at-scale profile -- the case TransformSystem's
// per-element scalar sincos does not accelerate. For the static (rotation == 0)
// workload the scalar fused front-end (SpatialCullSystem) remains preferable: it
// skips trig entirely, which a batched sincos cannot beat.
//
// Determinism / byte-identity: fast_math's scalar and SIMD sincos share one
// register-level kernel and agree BIT-FOR-BIT on FMA-capable targets
// (test_trig_simd_parity.cpp), so mat3FromTrsArray (SIMD body + scalar tail) is
// chunk/tile-invariant and bit-identical to a per-element scalar build using the
// same general formula. That is this system's deterministic reference (a
// single-threaded call), so a threaded driver that calls it per chunk merges
// bit-for-bit. It is NOT byte-identical to TransformSystem::run for rotation == 0:
// TransformSystem's zero-rotation fast path writes m01 = +0.0, while the general
// formula yields m01 = -(sy*sin 0) = -0.0. The values are numerically equal
// (-0.0 == +0.0) and downstream-irrelevant; only the stored sign bit differs.
//
// The build is tiled into small fixed stack buffers, so there is no hot-path
// heap allocation and the SoA scratch stays resident in cache.
template<class Provider, class Dim>
struct BatchedTransformSystem {
    static SystemResult run(
        std::span<const Transform<Provider, Dim>> transforms_,
        std::span<WorldTransform<Provider, Dim>> world_transforms_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isSystemResultCountRepresentable(transforms_.size()) ||
                !isSystemResultCountRepresentable(world_transforms_.size())) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            if (world_transforms_.size() < transforms_.size()) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = static_cast<U32>(transforms_.size()),
                    .write_count = static_cast<U32>(world_transforms_.size()),
                };
            }

            const auto* const transforms = transforms_.data();
            auto* const world_transforms = world_transforms_.data();
            const Usize transform_count = transforms_.size();

            // Tile so the SoA scratch stays in cache; kTile is a multiple of the
            // AVX2 width so only the final tile has a scalar remainder.
            constexpr Usize kTile = 256U;
            alignas(32) Vec2 translations[kTile];
            alignas(32) float rotations[kTile];
            alignas(32) Vec2 scales[kTile];
            alignas(32) Mat3 affines[kTile];

            for (Usize base = 0U; base < transform_count; base += kTile) {
                const Usize remaining = transform_count - base;
                const Usize tile = remaining < kTile ? remaining : kTile;

                for (Usize i = 0U; i < tile; ++i) {
                    const auto& transform = transforms[base + i];
                    translations[i] = Vec2{.x = transform.position_x, .y = transform.position_y};
                    rotations[i] = transform.rotation_radians;
                    scales[i] = Vec2{.x = transform.scale_x, .y = transform.scale_y};
                }

                MMath::mat3FromTrsArray(
                    translations, rotations, scales, affines, static_cast<std::int32_t>(tile));

                for (Usize i = 0U; i < tile; ++i) {
                    world_transforms[base + i] = {
                        .source_id = transforms[base + i].source_id,
                        .affine = affines[i],
                    };
                }
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(transform_count),
                .write_count = static_cast<U32>(transform_count),
            };
        }
    }
};

} // namespace Render2D
