#pragma once

#include "Render2D/Component/Batch.hpp"
#include "Render2D/Component/Command.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Meta/Domain.hpp"
#include "Render2D/System/SortKey.hpp"

#include <span>

namespace Render2D {

template<class Provider, class Dim>
struct BatchSystem {
    static SystemResult run(
        std::span<const DrawCommand<Provider, Dim>> draw_commands_,
        std::span<BatchCommand<Provider, Dim>> batch_commands_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isSystemResultCountRepresentable(draw_commands_.size()) ||
                !isSystemResultCountRepresentable(batch_commands_.size())) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            if (draw_commands_.empty()) {
                return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 0U};
            }
            if (batch_commands_.empty()) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = static_cast<U32>(draw_commands_.size()),
                    .write_count = 0U,
                };
            }

            Usize batch_index = 0U;
            batch_commands_[batch_index] = makeBatch(draw_commands_[0], 0U);

            for (Usize draw_index = 1U; draw_index < draw_commands_.size(); ++draw_index) {
                const auto& previous_draw = draw_commands_[draw_index - 1U];
                const auto& current_draw = draw_commands_[draw_index];
                if (canMerge(previous_draw, current_draw)) {
                    ++batch_commands_[batch_index].draw_count;
                    continue;
                }

                ++batch_index;
                if (batch_index >= batch_commands_.size()) {
                    return {
                        .code = SystemStatusCode::InsufficientCapacity,
                        .read_count = static_cast<U32>(draw_index),
                        .write_count = static_cast<U32>(batch_index),
                    };
                }
                batch_commands_[batch_index] = makeBatch(current_draw, static_cast<U32>(draw_index));
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(draw_commands_.size()),
                .write_count = static_cast<U32>(batch_index + 1U),
            };
        }
    }

private:
    static bool canMerge(
        const DrawCommand<Provider, Dim>& left_,
        const DrawCommand<Provider, Dim>& right_) noexcept
    {
        return drawCommandsHaveEqualBatchKey(left_, right_);
    }

    static BatchCommand<Provider, Dim> makeBatch(
        const DrawCommand<Provider, Dim>& draw_command_,
        U32 draw_first_) noexcept
    {
        return {
            .draw_first = draw_first_,
            .draw_count = 1U,
            .material_id = draw_command_.material_id,
            .texture_id = draw_command_.texture_id,
            .pipeline_id = 0U,
            .descriptor_id = 0U,
            .sort_key = draw_command_.sort_key,
            .flags = draw_command_.flags,
        };
    }
};

} // namespace Render2D
