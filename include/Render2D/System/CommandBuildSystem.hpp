#pragma once

#include "Render2D/Component/Command.hpp"
#include "Render2D/Component/Sprite.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Meta/Domain.hpp"
#include "Render2D/System/SortKey.hpp"

#include <span>

namespace Render2D {

template<class Provider, class Dim>
struct CommandBuildSystem {
    static SystemResult run(
        std::span<const VisibleItem<Provider, Dim>> visible_items_,
        std::span<const Sprite<Provider, Dim>> sprites_,
        std::span<DrawCommand<Provider, Dim>> draw_commands_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (draw_commands_.size() < visible_items_.size()) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = static_cast<U32>(visible_items_.size()),
                    .write_count = static_cast<U32>(draw_commands_.size()),
                };
            }

            for (Usize index = 0U; index < visible_items_.size(); ++index) {
                const auto source_index = visible_items_[index].source_index;
                if (source_index >= sprites_.size()) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(index),
                        .write_count = static_cast<U32>(index),
                    };
                }

                const auto& sprite = sprites_[source_index];
                draw_commands_[index] = {
                    .source_index = source_index,
                    .material_id = sprite.material_id,
                    .texture_id = sprite.texture_id,
                    .vertex_first = 0U,
                    .vertex_count = 4U,
                    .index_first = 0U,
                    .index_count = 6U,
                    .instance_first = static_cast<U32>(index),
                    .instance_count = 1U,
                    .sort_key = makeDrawSortKey(sprite.layer, sprite.material_id, sprite.texture_id, sprite.flags),
                    .layer = sprite.layer,
                    .flags = sprite.flags,
                };
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(visible_items_.size()),
                .write_count = static_cast<U32>(visible_items_.size()),
            };
        }
    }
};

} // namespace Render2D
