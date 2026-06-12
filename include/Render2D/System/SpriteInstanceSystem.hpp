#pragma once

#include "Render2D/Component/Batch.hpp"
#include "Render2D/Component/Command.hpp"
#include "Render2D/Component/Sprite.hpp"
#include "Render2D/Component/Transform.hpp"
#include "Render2D/Component/Upload.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Meta/Domain.hpp"

#include <algorithm>
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
                    .texture_generation = draw.texture_generation,
                    .material_id = draw.material_id,
                    .material_generation = draw.material_generation,
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

    static SystemResult runWithTextureRegions(
        std::span<const DrawCommand<Provider, Dim>> draw_commands_,
        std::span<const WorldTransform<Provider, Dim>> world_transforms_,
        std::span<const Sprite<Provider, Dim>> sprites_,
        std::span<const TextureAtlasRegion<Provider, Dim>> texture_regions_,
        std::span<SpriteInstance<Provider, Dim>> sprite_instances_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isSystemResultCountRepresentable(draw_commands_.size()) ||
                !isSystemResultCountRepresentable(world_transforms_.size()) ||
                !isSystemResultCountRepresentable(sprites_.size()) ||
                !isSystemResultCountRepresentable(texture_regions_.size()) ||
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
                const auto& sprite = sprites_[draw.source_index];
                if (!hasValidTextureRegion(sprite, draw, texture_regions_)) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
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
                float uv_min_x = kSpriteDefaultUvMin;
                float uv_min_y = kSpriteDefaultUvMin;
                float uv_max_x = kSpriteDefaultUvMax;
                float uv_max_y = kSpriteDefaultUvMax;
                const auto* region = findTextureRegion(
                    sprite.texture_region_id,
                    sprite.texture_region_generation,
                    texture_regions_);
                if (region != nullptr) {
                    uv_min_x = region->uv_min_x;
                    uv_min_y = region->uv_min_y;
                    uv_max_x = region->uv_max_x;
                    uv_max_y = region->uv_max_y;
                }

                sprite_instances_[draw.instance_first] = {
                    .transform_m00 = affine.m00,
                    .transform_m01 = affine.m01,
                    .transform_m02 = affine.m02,
                    .transform_m10 = affine.m10,
                    .transform_m11 = affine.m11,
                    .transform_m12 = affine.m12,
                    .uv_min_x = uv_min_x,
                    .uv_min_y = uv_min_y,
                    .uv_max_x = uv_max_x,
                    .uv_max_y = uv_max_y,
                    .source_index = draw.source_index,
                    .source_id = sprite.source_id,
                    .texture_id = draw.texture_id,
                    .texture_generation = draw.texture_generation,
                    .material_id = draw.material_id,
                    .material_generation = draw.material_generation,
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

private:
    static bool hasValidTextureRegion(
        const Sprite<Provider, Dim>& sprite_,
        const DrawCommand<Provider, Dim>& draw_,
        std::span<const TextureAtlasRegion<Provider, Dim>> texture_regions_) noexcept
    {
        if (sprite_.texture_region_id == 0U && sprite_.texture_region_generation == 0U) {
            return true;
        }

        const auto* region = findTextureRegion(
            sprite_.texture_region_id,
            sprite_.texture_region_generation,
            texture_regions_);
        return region != nullptr &&
            region->texture_id == draw_.texture_id &&
            region->texture_generation == draw_.texture_generation;
    }

    static const TextureAtlasRegion<Provider, Dim>* findTextureRegion(
        U32 region_id_,
        U32 generation_,
        std::span<const TextureAtlasRegion<Provider, Dim>> texture_regions_) noexcept
    {
        if (generation_ == 0U) {
            return nullptr;
        }
        if (region_id_ < texture_regions_.size() &&
            texture_regions_[region_id_].region_id == region_id_ &&
            texture_regions_[region_id_].generation == generation_) {
            return &texture_regions_[region_id_];
        }

        for (const auto& region : texture_regions_) {
            if (region.region_id == region_id_ && region.generation == generation_) {
                return &region;
            }
        }
        return nullptr;
    }
};

template<class Provider, class Dim>
struct TextureAtlasBuildSystem {
    static SystemResult run(
        std::span<const TextureAtlasItem<Provider, Dim>> items_,
        std::span<TextureAtlasRegion<Provider, Dim>> regions_,
        TextureAtlasBuildConfig config_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isSystemResultCountRepresentable(items_.size()) ||
                !isSystemResultCountRepresentable(regions_.size()) ||
                !isValidConfig(config_)) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            if (items_.empty()) {
                return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 0U};
            }
            if (regions_.size() < items_.size()) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = static_cast<U32>(items_.size()),
                    .write_count = static_cast<U32>(regions_.size()),
                };
            }

            U32 cursor_x = 0U;
            U32 cursor_y = 0U;
            U32 row_height = 0U;
            U32 write_count = 0U;
            for (Usize index = 0U; index < items_.size(); ++index) {
                const auto& item = items_[index];
                U32 padded_width = 0U;
                U32 padded_height = 0U;
                U32 padding = 0U;
                if (!makePaddedExtent(item, config_, padded_width, padded_height, padding)) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(index),
                        .write_count = write_count,
                    };
                }

                if (cursor_x != 0U && padded_width > config_.atlas_width - cursor_x) {
                    cursor_x = 0U;
                    if (cursor_y > 0xFFFFFFFFU - row_height) {
                        return {
                            .code = SystemStatusCode::InsufficientCapacity,
                            .read_count = static_cast<U32>(index),
                            .write_count = write_count,
                        };
                    }
                    cursor_y += row_height;
                    row_height = 0U;
                }
                if (cursor_y > config_.atlas_height ||
                    padded_height > config_.atlas_height - cursor_y) {
                    return {
                        .code = SystemStatusCode::InsufficientCapacity,
                        .read_count = static_cast<U32>(index),
                        .write_count = write_count,
                    };
                }

                const U32 x = cursor_x + padding;
                const U32 y = cursor_y + padding;
                regions_[write_count] = makeRegion(item, config_, x, y);
                ++write_count;

                cursor_x += padded_width;
                row_height = row_height < padded_height ? padded_height : row_height;
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(items_.size()),
                .write_count = write_count,
            };
        }
    }

    // First-Fit Decreasing Height shelf packing: place items tallest-first so a
    // shelf's wasted vertical space is bounded by similar-height neighbours,
    // which packs varied-size items far tighter than the input-order shelf in
    // run(). The caller owns the scratch order span (no allocation); output
    // regions are looked up by region id, so placement order is not observable
    // to consumers.
    static SystemResult runHeightSorted(
        std::span<const TextureAtlasItem<Provider, Dim>> items_,
        std::span<TextureAtlasRegion<Provider, Dim>> regions_,
        std::span<U32> scratch_order_,
        TextureAtlasBuildConfig config_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isSystemResultCountRepresentable(items_.size()) ||
                !isSystemResultCountRepresentable(regions_.size()) ||
                !isSystemResultCountRepresentable(scratch_order_.size()) ||
                !isValidConfig(config_)) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            if (items_.empty()) {
                return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 0U};
            }
            if (regions_.size() < items_.size() || scratch_order_.size() < items_.size()) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = static_cast<U32>(items_.size()),
                    .write_count = 0U,
                };
            }

            const Usize item_count = items_.size();
            const std::span<U32> ordered = scratch_order_.first(item_count);
            for (Usize index = 0U; index < item_count; ++index) {
                ordered[index] = static_cast<U32>(index);
            }
            std::sort(
                ordered.begin(),
                ordered.end(),
                [items_](U32 left_, U32 right_) noexcept {
                    const U32 left_height = items_[left_].height;
                    const U32 right_height = items_[right_].height;
                    return left_height != right_height ? left_height > right_height : left_ < right_;
                });

            U32 cursor_x = 0U;
            U32 cursor_y = 0U;
            U32 row_height = 0U;
            U32 write_count = 0U;
            for (Usize order_index = 0U; order_index < item_count; ++order_index) {
                const auto& item = items_[ordered[order_index]];
                U32 padded_width = 0U;
                U32 padded_height = 0U;
                U32 padding = 0U;
                if (!makePaddedExtent(item, config_, padded_width, padded_height, padding)) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(order_index),
                        .write_count = write_count,
                    };
                }

                if (cursor_x != 0U && padded_width > config_.atlas_width - cursor_x) {
                    cursor_x = 0U;
                    if (cursor_y > 0xFFFFFFFFU - row_height) {
                        return {
                            .code = SystemStatusCode::InsufficientCapacity,
                            .read_count = static_cast<U32>(order_index),
                            .write_count = write_count,
                        };
                    }
                    cursor_y += row_height;
                    row_height = 0U;
                }
                if (cursor_y > config_.atlas_height ||
                    padded_height > config_.atlas_height - cursor_y) {
                    return {
                        .code = SystemStatusCode::InsufficientCapacity,
                        .read_count = static_cast<U32>(order_index),
                        .write_count = write_count,
                    };
                }

                const U32 x = cursor_x + padding;
                const U32 y = cursor_y + padding;
                regions_[write_count] = makeRegion(item, config_, x, y);
                ++write_count;

                cursor_x += padded_width;
                row_height = row_height < padded_height ? padded_height : row_height;
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(item_count),
                .write_count = write_count,
            };
        }
    }

private:
    static bool isValidConfig(TextureAtlasBuildConfig config_) noexcept
    {
        return config_.atlas_width != 0U &&
            config_.atlas_height != 0U &&
            config_.atlas_generation != 0U &&
            config_.texture_generation != 0U;
    }

    static bool makePaddedExtent(
        const TextureAtlasItem<Provider, Dim>& item_,
        TextureAtlasBuildConfig config_,
        U32& out_width_,
        U32& out_height_,
        U32& out_padding_) noexcept
    {
        if (item_.generation == 0U ||
            item_.width == 0U ||
            item_.height == 0U ||
            config_.padding > 0xFFFFFFFFU - item_.padding) {
            return false;
        }

        out_padding_ = config_.padding + item_.padding;
        if (out_padding_ > (0xFFFFFFFFU / 2U) ||
            item_.width > 0xFFFFFFFFU - (out_padding_ * 2U) ||
            item_.height > 0xFFFFFFFFU - (out_padding_ * 2U)) {
            return false;
        }

        out_width_ = item_.width + out_padding_ * 2U;
        out_height_ = item_.height + out_padding_ * 2U;
        return out_width_ <= config_.atlas_width && out_height_ <= config_.atlas_height;
    }

    static TextureAtlasRegion<Provider, Dim> makeRegion(
        const TextureAtlasItem<Provider, Dim>& item_,
        TextureAtlasBuildConfig config_,
        U32 x_,
        U32 y_) noexcept
    {
        const float inv_width = 1.0F / static_cast<float>(config_.atlas_width);
        const float inv_height = 1.0F / static_cast<float>(config_.atlas_height);
        return {
            .region_id = item_.region_id,
            .generation = item_.generation,
            .atlas_id = config_.atlas_id,
            .atlas_generation = config_.atlas_generation,
            .texture_id = config_.texture_id,
            .texture_generation = config_.texture_generation,
            .x = x_,
            .y = y_,
            .width = item_.width,
            .height = item_.height,
            .uv_min_x = static_cast<float>(x_) * inv_width,
            .uv_min_y = static_cast<float>(y_) * inv_height,
            .uv_max_x = static_cast<float>(x_ + item_.width) * inv_width,
            .uv_max_y = static_cast<float>(y_ + item_.height) * inv_height,
            .flags = item_.flags | config_.flags,
        };
    }
};

template<class Provider, class Dim>
struct SpriteDrawPacketBuildSystem {
    static SystemResult run(
        std::span<const BatchCommand<Provider, Dim>> batch_commands_,
        std::span<const DrawCommand<Provider, Dim>> draw_commands_,
        std::span<const SpriteMaterialBinding<Provider, Dim>> material_bindings_,
        std::span<const SpriteTextureBinding<Provider, Dim>> texture_bindings_,
        std::span<SpriteDrawPacket<Provider, Dim>> sprite_draw_packets_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isSystemResultCountRepresentable(batch_commands_.size()) ||
                !isSystemResultCountRepresentable(draw_commands_.size()) ||
                !isSystemResultCountRepresentable(material_bindings_.size()) ||
                !isSystemResultCountRepresentable(texture_bindings_.size()) ||
                !isSystemResultCountRepresentable(sprite_draw_packets_.size())) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            if (batch_commands_.empty()) {
                return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 0U};
            }
            if (sprite_draw_packets_.size() < batch_commands_.size()) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = static_cast<U32>(batch_commands_.size()),
                    .write_count = static_cast<U32>(sprite_draw_packets_.size()),
                };
            }

            U32 write_count = 0U;
            for (Usize batch_index = 0U; batch_index < batch_commands_.size(); ++batch_index) {
                const auto& batch = batch_commands_[batch_index];
                const auto* material = findMaterialBinding(
                    batch.material_id,
                    batch.material_generation,
                    material_bindings_);
                const auto* texture = findTextureBinding(
                    batch.texture_id,
                    batch.texture_generation,
                    texture_bindings_);
                if (material == nullptr || texture == nullptr) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(batch_index),
                        .write_count = write_count,
                    };
                }

                SpriteDrawPacket<Provider, Dim> packet{};
                if (!makePacket(
                        batch,
                        static_cast<U32>(batch_index),
                        draw_commands_,
                        *material,
                        *texture,
                        packet)) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(batch_index),
                        .write_count = write_count,
                    };
                }

                sprite_draw_packets_[write_count] = packet;
                ++write_count;
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(batch_commands_.size()),
                .write_count = write_count,
            };
        }
    }

private:
    static const SpriteMaterialBinding<Provider, Dim>* findMaterialBinding(
        U32 material_id_,
        U32 material_generation_,
        std::span<const SpriteMaterialBinding<Provider, Dim>> bindings_) noexcept
    {
        if (material_id_ < bindings_.size() &&
            bindings_[material_id_].material_id == material_id_ &&
            bindings_[material_id_].material_generation == material_generation_) {
            return &bindings_[material_id_];
        }

        for (const auto& binding : bindings_) {
            if (binding.material_id == material_id_ &&
                binding.material_generation == material_generation_) {
                return &binding;
            }
        }
        return nullptr;
    }

    static const SpriteTextureBinding<Provider, Dim>* findTextureBinding(
        U32 texture_id_,
        U32 texture_generation_,
        std::span<const SpriteTextureBinding<Provider, Dim>> bindings_) noexcept
    {
        if (texture_id_ < bindings_.size() &&
            bindings_[texture_id_].texture_id == texture_id_ &&
            bindings_[texture_id_].texture_generation == texture_generation_) {
            return &bindings_[texture_id_];
        }

        for (const auto& binding : bindings_) {
            if (binding.texture_id == texture_id_ &&
                binding.texture_generation == texture_generation_) {
                return &binding;
            }
        }
        return nullptr;
    }

    static bool makePacket(
        const BatchCommand<Provider, Dim>& batch_,
        U32 batch_index_,
        std::span<const DrawCommand<Provider, Dim>> draw_commands_,
        const SpriteMaterialBinding<Provider, Dim>& material_,
        const SpriteTextureBinding<Provider, Dim>& texture_,
        SpriteDrawPacket<Provider, Dim>& out_packet_) noexcept
    {
        if (batch_.draw_count == 0U ||
            batch_.draw_first > draw_commands_.size() ||
            batch_.draw_count > draw_commands_.size() - batch_.draw_first ||
            material_.pipeline_generation == 0U ||
            texture_.descriptor_generation == 0U ||
            texture_.descriptor_count == 0U) {
            return false;
        }

        const auto& first_draw = draw_commands_[batch_.draw_first];
        U32 instance_count = 0U;
        for (U32 offset = 0U; offset < batch_.draw_count; ++offset) {
            const auto& draw = draw_commands_[batch_.draw_first + offset];
            if (draw.instance_count > 0xFFFFFFFFU - instance_count ||
                first_draw.instance_first > 0xFFFFFFFFU - instance_count) {
                return false;
            }
            if (!isCompatibleDraw(first_draw, draw, batch_) ||
                draw.instance_count == 0U ||
                draw.instance_first != first_draw.instance_first + instance_count) {
                return false;
            }
            instance_count += draw.instance_count;
        }

        out_packet_ = {
            .batch_index = batch_index_,
            .draw_first = batch_.draw_first,
            .draw_count = batch_.draw_count,
            .instance_first = first_draw.instance_first,
            .instance_count = instance_count,
            .vertex_first = first_draw.vertex_first,
            .vertex_count = first_draw.vertex_count,
            .index_first = first_draw.index_first,
            .index_count = first_draw.index_count,
            .material_id = batch_.material_id,
            .material_generation = batch_.material_generation,
            .texture_id = batch_.texture_id,
            .texture_generation = batch_.texture_generation,
            .pipeline_id = material_.pipeline_id,
            .pipeline_generation = material_.pipeline_generation,
            .descriptor_id = texture_.descriptor_id,
            .descriptor_generation = texture_.descriptor_generation,
            .descriptor_first = texture_.descriptor_first,
            .descriptor_count = texture_.descriptor_count,
            .flags = batch_.flags | material_.flags | texture_.flags,
        };
        return true;
    }

    static bool isCompatibleDraw(
        const DrawCommand<Provider, Dim>& first_draw_,
        const DrawCommand<Provider, Dim>& draw_,
        const BatchCommand<Provider, Dim>& batch_) noexcept
    {
        return draw_.material_id == batch_.material_id &&
            draw_.material_generation == batch_.material_generation &&
            draw_.texture_id == batch_.texture_id &&
            draw_.texture_generation == batch_.texture_generation &&
            draw_.vertex_first == first_draw_.vertex_first &&
            draw_.vertex_count == first_draw_.vertex_count &&
            draw_.index_first == first_draw_.index_first &&
            draw_.index_count == first_draw_.index_count &&
            draw_.layer == first_draw_.layer &&
            draw_.flags == first_draw_.flags;
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
