#pragma once

#include "Render2D/Component/Command.hpp"
#include "Render2D/Component/Sprite.hpp"
#include "Render2D/Component/Transform.hpp"
#include "Render2D/Component/Upload.hpp"
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

template<class Provider, class Dim>
struct SpriteInstanceUploadSystem {
    static SystemResult run(
        std::span<const SpriteInstanceUploadCommand<Provider, Dim>> sprite_upload_commands_,
        std::span<const SpriteInstance<Provider, Dim>> sprite_instances_,
        std::span<UploadCommand<Provider, Dim>> upload_commands_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isSystemResultCountRepresentable(sprite_upload_commands_.size()) ||
                !isSystemResultCountRepresentable(sprite_instances_.size()) ||
                !isSystemResultCountRepresentable(upload_commands_.size())) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            if (sprite_upload_commands_.empty()) {
                return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 0U};
            }

            U32 write_count = 0U;
            for (Usize read_index = 0U; read_index < sprite_upload_commands_.size(); ++read_index) {
                if (write_count >= upload_commands_.size()) {
                    return {
                        .code = SystemStatusCode::InsufficientCapacity,
                        .read_count = static_cast<U32>(read_index),
                        .write_count = write_count,
                    };
                }

                const auto& command = sprite_upload_commands_[read_index];
                U64 source_offset = 0U;
                U64 byte_count = 0U;
                if (!makeInstanceByteRange(command, sprite_instances_.size(), source_offset, byte_count)) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(read_index),
                        .write_count = write_count,
                    };
                }

                upload_commands_[write_count] = {
                    .resource_id = command.destination_buffer_id,
                    .source_offset = source_offset,
                    .destination_offset = command.destination_offset,
                    .byte_count = byte_count,
                    .upload_kind = kUploadKindSpriteInstance,
                    .flags = command.flags,
                };
                ++write_count;
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(sprite_upload_commands_.size()),
                .write_count = write_count,
            };
        }
    }

private:
    static constexpr U64 kMaxU64 = 0xFFFFFFFFFFFFFFFFULL;
    static constexpr U64 kInstanceByteSize = static_cast<U64>(sizeof(SpriteInstance<Provider, Dim>));

    static bool makeInstanceByteRange(
        const SpriteInstanceUploadCommand<Provider, Dim>& command_,
        Usize instance_count_,
        U64& out_source_offset_,
        U64& out_byte_count_) noexcept
    {
        if (command_.instance_count == 0U ||
            command_.destination_generation == 0U ||
            command_.instance_first > instance_count_ ||
            command_.instance_count > instance_count_ - command_.instance_first ||
            static_cast<U64>(command_.instance_first) > kMaxU64 / kInstanceByteSize ||
            static_cast<U64>(command_.instance_count) > kMaxU64 / kInstanceByteSize) {
            return false;
        }

        out_source_offset_ = static_cast<U64>(command_.instance_first) * kInstanceByteSize;
        out_byte_count_ = static_cast<U64>(command_.instance_count) * kInstanceByteSize;
        return command_.destination_offset <= kMaxU64 - out_byte_count_;
    }
};

} // namespace Render2D
