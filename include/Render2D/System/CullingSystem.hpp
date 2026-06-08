#pragma once

#include "Render2D/Component/Bounds.hpp"
#include "Render2D/Component/Camera.hpp"
#include "Render2D/Component/Command.hpp"
#include "Render2D/Component/Sprite.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Meta/Domain.hpp"

#include <span>

namespace Render2D {

template<class Provider, class Dim>
struct CullingSystem {
    static SystemResult run(
        const Camera<Provider, Dim>& camera_,
        std::span<const WorldBounds<Provider, Dim>> world_bounds_,
        std::span<const VisibilityMask<Provider, Dim>> visibility_masks_,
        std::span<VisibleItem<Provider, Dim>> visible_items_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!visibility_masks_.empty() && visibility_masks_.size() != world_bounds_.size()) {
                return {
                    .code = SystemStatusCode::InvalidInput,
                    .read_count = static_cast<U32>(world_bounds_.size()),
                    .write_count = 0U,
                };
            }

            Usize write_index = 0U;
            const auto camera_bounds = cameraBounds(camera_);
            for (Usize index = 0U; index < world_bounds_.size(); ++index) {
                const auto mask = visibility_masks_.empty() ? camera_.layer_mask : visibility_masks_[index].mask;
                if ((mask & camera_.layer_mask) == 0U || !intersects(world_bounds_[index].bounds, camera_bounds)) {
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
                .read_count = static_cast<U32>(world_bounds_.size()),
                .write_count = static_cast<U32>(write_index),
            };
        }
    }

private:
    static Aabb2 cameraBounds(const Camera<Provider, Dim>& camera_) noexcept
    {
        const auto half_width = camera_.viewport_width * 0.5F;
        const auto half_height = camera_.viewport_height * 0.5F;
        return {
            .min_x = camera_.position_x - half_width,
            .min_y = camera_.position_y - half_height,
            .max_x = camera_.position_x + half_width,
            .max_y = camera_.position_y + half_height,
        };
    }

    static bool intersects(const Aabb2& left_, const Aabb2& right_) noexcept
    {
        return left_.min_x <= right_.max_x &&
            left_.max_x >= right_.min_x &&
            left_.min_y <= right_.max_y &&
            left_.max_y >= right_.min_y;
    }
};

} // namespace Render2D
