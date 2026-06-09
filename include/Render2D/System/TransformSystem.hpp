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
            for (Usize index = 0U; index < transform_count; ++index) {
                writeWorldTransform(transforms[index], world_transforms[index]);
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(transform_count),
                .write_count = static_cast<U32>(transform_count),
            };
        }
    }

    static SystemResult runDirty(
        std::span<const Transform<Provider, Dim>> transforms_,
        std::span<const TransformDirtyItem<Provider, Dim>> dirty_items_,
        std::span<WorldTransform<Provider, Dim>> world_transforms_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isSystemResultCountRepresentable(transforms_.size()) ||
                !isSystemResultCountRepresentable(dirty_items_.size()) ||
                !isSystemResultCountRepresentable(world_transforms_.size())) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }

            const auto* const transforms = transforms_.data();
            const auto* const dirty_items = dirty_items_.data();
            auto* const world_transforms = world_transforms_.data();
            const Usize transform_count = transforms_.size();
            const Usize world_transform_count = world_transforms_.size();
            const Usize dirty_count = dirty_items_.size();
            for (Usize dirty_index = 0U; dirty_index < dirty_count; ++dirty_index) {
                const Usize source_index = dirty_items[dirty_index].source_index;
                if (source_index >= transform_count || source_index >= world_transform_count) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(dirty_index),
                        .write_count = static_cast<U32>(dirty_index),
                    };
                }

                writeWorldTransform(transforms[source_index], world_transforms[source_index]);
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(dirty_count),
                .write_count = static_cast<U32>(dirty_count),
            };
        }
    }

private:
    static void writeWorldTransform(
        const Transform<Provider, Dim>& transform_,
        WorldTransform<Provider, Dim>& out_world_transform_) noexcept
    {
        if (transform_.rotation_radians == 0.0F) {
            out_world_transform_ = {
                .source_id = transform_.source_id,
                .affine = {
                    .m00 = transform_.scale_x,
                    .m01 = 0.0F,
                    .m02 = transform_.position_x,
                    .m10 = 0.0F,
                    .m11 = transform_.scale_y,
                    .m12 = transform_.position_y,
                    .m20 = 0.0F,
                    .m21 = 0.0F,
                    .m22 = 1.0F,
                },
            };
            return;
        }

        MMath::SinCos sin_cos{};
        MMath::sincos(MMath::Angle{.value = transform_.rotation_radians}, &sin_cos);
        out_world_transform_ = {
            .source_id = transform_.source_id,
            .affine = {
                .m00 = sin_cos.cos * transform_.scale_x,
                .m01 = -sin_cos.sin * transform_.scale_y,
                .m02 = transform_.position_x,
                .m10 = sin_cos.sin * transform_.scale_x,
                .m11 = sin_cos.cos * transform_.scale_y,
                .m12 = transform_.position_y,
                .m20 = 0.0F,
                .m21 = 0.0F,
                .m22 = 1.0F,
            },
        };
    }
};

} // namespace Render2D
