#pragma once

#include "Render2D/Component/Transform.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Meta/Domain.hpp"

#include <cmath>
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
                const auto cos_rotation = std::cos(transform.rotation_radians);
                const auto sin_rotation = std::sin(transform.rotation_radians);

                world_transforms_[index] = WorldTransform<Provider, Dim>{
                    .source_id = transform.source_id,
                    .affine = {
                        .m00 = cos_rotation * transform.scale_x,
                        .m01 = -sin_rotation * transform.scale_y,
                        .m02 = transform.position_x,
                        .m10 = sin_rotation * transform.scale_x,
                        .m11 = cos_rotation * transform.scale_y,
                        .m12 = transform.position_y,
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
