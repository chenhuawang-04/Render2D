#pragma once

#include "Render2D/Component/Command.hpp"
#include "Render2D/Component/Sprite.hpp"
#include "Render2D/Component/Transform.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Meta/Domain.hpp"

#include <span>

namespace Render2D {

inline constexpr float kSpriteDefaultUvMin = 0.0F;
inline constexpr float kSpriteDefaultUvMax = 1.0F;

template<class Provider, class Dim>
struct SpriteInstanceBuildSystem {
    static SystemResult run(
        std::span<const DrawCommand<Provider, Dim>> draw_commands_,
        std::span<const WorldTransform<Provider, Dim>> world_transforms_,
        std::span<const Sprite<Provider, Dim>> sprites_,
        std::span<SpriteInstance<Provider, Dim>> sprite_instances_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isSystemResultCountRepresentable(draw_commands_.size()) ||
                !isSystemResultCountRepresentable(world_transforms_.size()) ||
                !isSystemResultCountRepresentable(sprites_.size()) ||
                !isSystemResultCountRepresentable(sprite_instances_.size())) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            if (draw_commands_.empty()) {
                return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 0U};
            }

            const U32 draw_count = static_cast<U32>(draw_commands_.size());
            for (U32 draw_index = 0U; draw_index < draw_count; ++draw_index) {
                const auto& draw = draw_commands_[draw_index];
                if (draw.instance_count != 1U ||
                    draw.source_index >= sprites_.size() ||
                    draw.source_index >= world_transforms_.size()) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = draw_index,
                        .write_count = 0U,
                    };
                }
                if (draw.instance_first >= sprite_instances_.size()) {
                    return {
                        .code = SystemStatusCode::InsufficientCapacity,
                        .read_count = draw_index,
                        .write_count = 0U,
                    };
                }
            }

            for (U32 draw_index = 0U; draw_index < draw_count; ++draw_index) {
                const auto& draw = draw_commands_[draw_index];
                const auto& sprite = sprites_[draw.source_index];
                const auto& world = world_transforms_[draw.source_index];
                const auto& affine = world.affine;

                sprite_instances_[draw.instance_first] = {
                    .transform_m00 = affine.m00,
                    .transform_m01 = affine.m01,
                    .transform_m02 = affine.m02,
                    .transform_m10 = affine.m10,
                    .transform_m11 = affine.m11,
                    .transform_m12 = affine.m12,
                    .uv_min_x = kSpriteDefaultUvMin,
                    .uv_min_y = kSpriteDefaultUvMin,
                    .uv_max_x = kSpriteDefaultUvMax,
                    .uv_max_y = kSpriteDefaultUvMax,
                    .source_index = draw.source_index,
                    .source_id = sprite.source_id,
                    .texture_id = draw.texture_id,
                    .material_id = draw.material_id,
                    .color_rgba8 = sprite.color_rgba8,
                    .sort_key = draw.sort_key,
                    .layer = draw.layer,
                    .flags = draw.flags,
                };
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = draw_count,
                .write_count = draw_count,
            };
        }
    }
};

} // namespace Render2D
