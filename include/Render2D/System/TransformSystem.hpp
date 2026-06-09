#pragma once

#include "Render2D/Component/Transform.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Meta/Domain.hpp"

#include <span>

namespace Render2D {

template<class Provider, class Dim>
struct TransformSystem {
    static SystemResult run(
        std::span<const Transform<Provider, Dim>> transforms_,
        std::span<WorldTransform<Provider, Dim>> world_transforms_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (world_transforms_.size() < transforms_.size()) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = static_cast<U32>(transforms_.size()),
                    .write_count = static_cast<U32>(world_transforms_.size()),
                };
            }

            for (Usize index = 0U; index < transforms_.size(); ++index) {
                const auto& transform = transforms_[index];
                MMath::SinCos sin_cos{};
                MMath::sincos(MMath::Angle{.value = transform.rotation_radians}, &sin_cos);

                world_transforms_[index] = WorldTransform<Provider, Dim>{
                    .source_id = transform.source_id,
                    .affine = {
                        .m00 = sin_cos.cos * transform.scale_x,
                        .m01 = -sin_cos.sin * transform.scale_y,
                        .m02 = transform.position_x,
                        .m10 = sin_cos.sin * transform.scale_x,
                        .m11 = sin_cos.cos * transform.scale_y,
                        .m12 = transform.position_y,
                        .m20 = 0.0F,
                        .m21 = 0.0F,
                        .m22 = 1.0F,
                    },
                };
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(transforms_.size()),
                .write_count = static_cast<U32>(transforms_.size()),
            };
        }
    }
};

} // namespace Render2D
